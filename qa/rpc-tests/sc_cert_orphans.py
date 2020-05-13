#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, dump_ordered_tips
import os
from decimal import Decimal
import pprint
import time

DEBUG_MODE = 1
NUMB_OF_NODES = 4
EPOCH_LENGTH = 5
CERT_FEE = Decimal('0.00015')


class sc_cert_orphans(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-logtimemicros=1']] * NUMB_OF_NODES)

        for k in range(0, NUMB_OF_NODES-1):
            connect_nodes_bi(self.nodes, k, k+1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def getEpochData(self, scid, node_idx = 0):
        sc_creating_height = self.nodes[node_idx].getscinfo(scid)['created at block height']
        current_height = self.nodes[node_idx].getblockcount()
        epoch_number = (current_height - sc_creating_height + 1) // EPOCH_LENGTH - 1
        mark_logs("Current height {}, Sc creation height {}, epoch length {} --> current epoch number {}"
                  .format(current_height, sc_creating_height, EPOCH_LENGTH, epoch_number), self.nodes, DEBUG_MODE)
        epoch_block_hash = self.nodes[node_idx].getblockhash(sc_creating_height - 1 + ((epoch_number + 1) * EPOCH_LENGTH))
        return epoch_block_hash, epoch_number

    def run_test(self):

        '''
        #  (1) Node0 create 2 sidechains with 10.0 coins each
            - reach epoch 0
        #  (2) Node0 sends fund to node1 ---> tx1
        #  (3) Node1 create a certificate using the unconfirmed coins of tx1 in mempool  ---> cert1
        #  (4) Node1 tries to use unconfirmed cert1 change for sending funds to node2, but will fail since an unconfirmed
        #  (5) Node1 tries to do it via a rawtransaction, but that will result by a refusal to be added in mempool
        #  (6) Node1 tries to create a cert using the same unconfirmed cert1 change, but fails too 
        #  (7) Node1 tries to do it via a rawcertificate, but that again will result by a refusal to be added in mempool
           -  a block is mined
        #  (8) Node1 retries to send the same rawtransaction, this time should succeed because cert change is now confirmed --> tx2
        #  (9) Node1 retries to send the same rawcertificate, this time will fail because double spends the same UTXO of tx2
        # (10) Node1 retries once more with the proper UTXO from tx2 ---> cert2
           -  a block is mined
        # (11) Node0 invalidates the latest block, tx2 and cert2 are restored in mempool
        # (12) Node0 invalidates one more block, tx1 and cert1 are restored in mempool but this will make tx2 and cert2 disappear
        # (13) Node0 reconsiders last invalidated block in order to allow network alignemnt when other nodes will prevail 
        '''
        # side chain id
        scid_1 = "1111"
        scid_2 = "2222"

        # cross chain transfer amounts
        creation_amount = Decimal("10.0")

        mark_logs("Node0 generates 220 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(220)
        self.sync_all()

        # (1) node0 create sidechains with 10.0 coins each
        mark_logs("Node0 creates SC {} and {}".format(scid_1, scid_2), self.nodes, DEBUG_MODE)
        creating_tx_1 = self.nodes[0].sc_create(scid_1, EPOCH_LENGTH, "dada", creation_amount, "abcdef")
        creating_tx_2 = self.nodes[0].sc_create(scid_2, EPOCH_LENGTH, "baba", creation_amount, "101010")
        self.sync_all()

        mark_logs("Node0 generates 5 blocks to achieve end of epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()
        epoch_block_hash, epoch_number = self.getEpochData(scid_1);

        # (2) node0 sends fund to node1, the resulting tx1 is in mempool
        taddr1 = self.nodes[1].getnewaddress()
        amount1 = Decimal("0.5")
        mark_logs("Node0 sends {} coins to Node1".format(amount1), self.nodes, DEBUG_MODE)
        tx1 = self.nodes[0].sendtoaddress(taddr1, amount1)
        mark_logs("======> tx1 = {}".format(tx1), self.nodes, DEBUG_MODE)
        self.sync_all()

        # (3) node1 create cert1 using the unconfirmed coins in mempool 
        pkh_node2 = self.nodes[2].getnewaddress("", True)
        bwt_amount = Decimal("1.0")
        amounts = [{"pubkeyhash": pkh_node2, "amount": bwt_amount}]
        mark_logs("Node1 sends a certificate for SC {} using unconfirmed UTXO from tx1".format(scid_1), self.nodes, DEBUG_MODE)
        try:
            cert1 = self.nodes[1].send_certificate(scid_1, epoch_number, epoch_block_hash, amounts, CERT_FEE)
            mark_logs("======> cert1 = {}".format(cert1), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)
        self.sync_all()

        # check mutual dependancies
        mark_logs("Check tx1 and cert1 are in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, tx1 in self.nodes[2].getrawmempool())
        assert_equal(True, cert1 in self.nodes[2].getrawmempool())
        mp = self.nodes[1].getrawmempool(True)
        #pprint.pprint(mp)
        dep_cert = mp[cert1]['depends']
        mark_logs("check that cert1 depends on tx1 {}".format(tx1), self.nodes, DEBUG_MODE)
        assert_true(tx1 in dep_cert)

        # (4) node1 try to use its unconfirmed cert1 change for sending funds to node2, but will fail since an unconfirmed
        #     certificate change is not usable
        taddr2 = self.nodes[2].getnewaddress()
        amount2 = Decimal("0.4")
        mark_logs("Node1 try to send {} coins to Node2 using unconfirmed change from cert1, expecting failure".format(amount2), self.nodes, DEBUG_MODE)
        try:
            tx2 = self.nodes[1].sendtoaddress(taddr2, amount2)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("{}".format(errorString), self.nodes, DEBUG_MODE)
            assert_true("Insufficient funds" in errorString)

        # (5) try to do it via a rawtransaction, but that will result by a refusal to be added in mempool
        inputs  = [{'txid' : cert1, 'vout' : 0}]
        change = amount1 - amount2 - Decimal("0.0002")
        outputs = { taddr2 : amount2, self.nodes[1].getnewaddress() : change  }
        mark_logs("Node1 try to do the same using a raw transaction, expecting failure".format(amount2), self.nodes, DEBUG_MODE)
        try:
            rawtx = self.nodes[1].createrawtransaction(inputs, outputs)
            rawtx = self.nodes[1].signrawtransaction(rawtx)
            self.nodes[1].sendrawtransaction(rawtx['hex'])
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("{}".format(errorString), self.nodes, DEBUG_MODE)
            assert_true("unconfirmed output" in errorString)

        # (6) node1 tries to create a cert2 using the same unconfirmed change 
        amounts = []
        mark_logs("Node1 tries to sends a certificate for SC {} using unconfirmed change from cert1, expecting failure".format(scid_2), self.nodes, DEBUG_MODE)
        try:
            cert2 = self.nodes[1].send_certificate(scid_2, epoch_number, epoch_block_hash, amounts, CERT_FEE)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        # (7) try to do it via a rawcertificate, but that again will result by a refusal to be added in mempool
        mark_logs("Node1 try to do the same using a raw certificate, expecting failure".format(amount2), self.nodes, DEBUG_MODE)
        inputs  = [{'txid' : cert1, 'vout' : 0}]
        outputs = { self.nodes[1].getnewaddress() : change }
        params = {"scid": scid_2, "endEpochBlockHash": epoch_block_hash, "withdrawalEpochNumber": epoch_number}
        try:
            rawcert    = self.nodes[1].createrawcertificate(inputs, outputs, {}, params)
            signed_cert = self.nodes[1].signrawcertificate(rawcert)
            #pprint.pprint(self.nodes[1].decoderawcertificate(signed_cert['hex']))
            rawcert = self.nodes[1].sendrawcertificate(signed_cert['hex'])
            assert_true(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        self.sync_all()

        mark_logs("Node0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # (8) retry to send the same rawtransaction, this time should succeed because cert change is now confirmed
        mark_logs("Node1 retry to send {} coins to Node2 via the raw transaction that was failing before".format(amount2), self.nodes, DEBUG_MODE)
        try:
            tx2 = self.nodes[1].sendrawtransaction(rawtx['hex'])
            mark_logs("======> tx2 = {}".format(tx2), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("{}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        # (9) retry to send the same rawcertificate, this time will fail because double spends the same UTXO of the tx2
        mark_logs("Node1 retry to send the same raw certificate that was failig before, expecting failure because it now would double spend", self.nodes, DEBUG_MODE)
        try:
            rawcert = self.nodes[1].sendrawcertificate(signed_cert['hex'])
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("{}".format(errorString), self.nodes, DEBUG_MODE)

        # (10) retry once more with the proper UTXO
        mark_logs("Node1 sends a certificate for SC {} using unconfirmed UTXO from tx2 change".format(scid_2), self.nodes, DEBUG_MODE)
        listunspent = self.nodes[1].listunspent(0)
        assert_equal(listunspent[0]['txid'], tx2)
        #pprint.pprint(listunspent)
        change = listunspent[0]['amount'] - CERT_FEE
        inputs  = [{'txid' : tx2, 'vout' : listunspent[0]['vout']}]
        outputs = { self.nodes[1].getnewaddress() : change }
        params = {"scid": scid_2, "endEpochBlockHash": epoch_block_hash, "withdrawalEpochNumber": epoch_number}
        try:
            rawcert    = self.nodes[1].createrawcertificate(inputs, outputs, {}, params)
            signed_cert = self.nodes[1].signrawcertificate(rawcert)
            #pprint.pprint(self.nodes[1].decoderawcertificate(signed_cert['hex']))
            cert2 = self.nodes[1].sendrawcertificate(signed_cert['hex'])
            mark_logs("======> cert2 = {}".format(cert2), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert_true(False)

        self.sync_all()

        mark_logs("Check tx2 and cert2 are in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, tx2 in self.nodes[2].getrawmempool())
        assert_equal(True, cert2 in self.nodes[2].getrawmempool())
        mp = self.nodes[1].getrawmempool(True)
        dep_cert = mp[cert2]['depends']
        mark_logs("check that cert2 depends on tx2 {}".format(tx2), self.nodes, DEBUG_MODE)
        assert_true(tx2 in dep_cert)

        mark_logs("Node0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Check mempools are empty", self.nodes, DEBUG_MODE)
        assert_equal(0, len(self.nodes[0].getrawmempool()))
        assert_equal(0, len(self.nodes[3].getrawmempool()))

        # (11) Node0 invalidates the latest block, tx2 and cert2 are restored in mempool
        block_inv = self.nodes[0].getbestblockhash()
        mark_logs("Node 0 invalidates latest block with height = {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        self.nodes[0].invalidateblock(block_inv)
        sync_mempools(self.nodes[0:1])
        mark_logs("Check tx2 and cert2 are back in mempool of Node0", self.nodes, DEBUG_MODE)
        assert_equal(True, tx2 in self.nodes[0].getrawmempool())
        assert_equal(True, cert2 in self.nodes[0].getrawmempool())

        # (12) Node0 invalidates one more block, tx1 and cert1 are restored in mempool but this will make tx2 and cert2 disappear
        # tx2 is removed from mempool because it spends cert1 change that is is now unconfirmed, and cert2 is removed
        # since it spends output from tx2 
        block_inv = self.nodes[0].getbestblockhash()
        mark_logs("Node 0 invalidates latest block with height = {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        self.nodes[0].invalidateblock(block_inv)
        sync_mempools(self.nodes[0:1])
        mark_logs("Check tx1 and cert1 are back in mempool of Node0", self.nodes, DEBUG_MODE)
        assert_equal(True, tx1 in self.nodes[0].getrawmempool())
        assert_equal(True, cert1 in self.nodes[0].getrawmempool())
        mark_logs("Check tx2 and cert2 are no more in mempool of Node0", self.nodes, DEBUG_MODE)
        assert_equal(False, tx2 in self.nodes[0].getrawmempool())
        assert_equal(False, cert2 in self.nodes[0].getrawmempool())

        mark_logs("Node0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        sync_mempools(self.nodes[0:1])

        # (13) Node0 reconsiders last invalidated block in order to allow network alignemnt when other nodes will prevail 
        mark_logs("Node 0 reconsider last invalidated block", self.nodes, DEBUG_MODE)
        self.nodes[0].reconsiderblock(block_inv)
        self.sync_all()

        mark_logs("Check mempools are empty", self.nodes, DEBUG_MODE)
        assert_equal(0, len(self.nodes[0].getrawmempool()))
        assert_equal(0, len(self.nodes[3].getrawmempool()))

        mark_logs("Node3 generates 2 blocks prevailing over Node0 and realigning the network", self.nodes, DEBUG_MODE)
        self.nodes[3].generate(2)
        self.sync_all()

        # verify network is aligned
        mark_logs("verifying that network is realigned", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[0].getscinfo(),        self.nodes[3].getscinfo())
        assert_equal(self.nodes[0].getrawmempool(),    self.nodes[3].getrawmempool())
        assert_equal(self.nodes[0].getblockcount(),    self.nodes[3].getblockcount())
        assert_equal(self.nodes[0].getbestblockhash(), self.nodes[3].getbestblockhash())


if __name__ == '__main__':
    sc_cert_orphans().main()
