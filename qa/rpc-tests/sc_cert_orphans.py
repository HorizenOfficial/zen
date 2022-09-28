#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import MINIMAL_SC_HEIGHT, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_equal, initialize_chain_clean, get_epoch_data, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, dump_ordered_tips, \
    swap_bytes
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal
import pprint
import time

DEBUG_MODE = 1
NUMB_OF_NODES = 4
EPOCH_LENGTH = 5
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
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
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib', '-scproofqueuesize=0', '-logtimemicros=1']] * NUMB_OF_NODES)

        for k in range(0, NUMB_OF_NODES-1):
            connect_nodes_bi(self.nodes, k, k+1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        #  (1) Node0 create 2 sidechains with 10.0 coins each
            - reach epoch 0
        #  (2) Node0 sends fund to node1 ---> tx1
        #  (3) Node1 create a certificate using the unconfirmed coins of tx1 in mempool  ---> cert1
        #  (4) Node1 tries to use unconfirmed cert1 change for sending funds to node2, but will fail since an unconfirmed
        #      cert change can not be spent by a tx
        #  (5) Node1 tries to do it via a rawtransaction, but that will result by a refusal to be added in mempool
        #  (6) Node1 creates a cert2 using the same unconfirmed cert1 change, this is ok
        #  (7) Node1 tries to do the same via a rawcertificate, but it will be dropped since a cert for that SC is already in mempool
           -  a block is mined

        #  (8) Node1 send coins to Node2 ---> tx2 using confirmed change of cert2
           -  a block is mined

        #  (9) Node0 invalidates the latest block, tx2 is restored in mempool
        # (10) Node0 invalidates one more block, tx1, cert1, cert2 are restored in mempool but tx2 disappears
        # (11) Node0 reconsiders last invalidated block in order to allow network alignemnt when other nodes will prevail 
        # (12) Node3  generates 2 blocks prevailing over Node0 and realigning the network 
        '''

        # cross chain transfer amounts
        creation_amount = Decimal("10.0")

        mark_logs("Node0 generates {} block".format(MINIMAL_SC_HEIGHT), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(MINIMAL_SC_HEIGHT)
        self.sync_all()

        # (1) node0 create sidechains with 10.0 coins each
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)

        vk_1 = mcTest.generate_params("sc1")
        constant_1 = generate_random_field_element_hex()

        vk_2 = mcTest.generate_params("sc2")
        constant_2 = generate_random_field_element_hex()

        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk_1,
            "constant": constant_1
        }

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx_1 = ret['txid']
        scid_1 = ret['scid']
        mark_logs("Node0 created SC id: {}".format(scid_1), self.nodes, DEBUG_MODE)
        self.sync_all()

        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "baba",
            "amount": creation_amount,
            "wCertVk": vk_2,
            "constant": constant_2,
        }

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx_2 = ret['txid']
        scid_2 = ret['scid']
        mark_logs("Node0 created SC id: {}".format(scid_2), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node0 generates 5 blocks to achieve end of epoch", self.nodes, DEBUG_MODE)
        prev_epoch_block_hash = self.nodes[0].getblockhash(self.nodes[0].getblockcount())
        self.nodes[0].generate(5)
        self.sync_all()
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid_1, self.nodes[0], EPOCH_LENGTH)

        # (2) node0 sends fund to node1, the resulting tx1 is in mempool
        taddr1 = self.nodes[1].getnewaddress()
        amount1 = Decimal("0.5")
        mark_logs("Node0 sends {} coins to Node1".format(amount1), self.nodes, DEBUG_MODE)
        tx1 = self.nodes[0].sendtoaddress(taddr1, amount1)
        mark_logs("======> tx1 = {}".format(tx1), self.nodes, DEBUG_MODE)
        self.sync_all()

        # (3) node1 create cert1 using the unconfirmed coins in mempool 
        addr_node2 = self.nodes[2].getnewaddress()
        bwt_amount = Decimal("1.0")
        amounts = [{"address": addr_node2, "amount": bwt_amount}]

        #Create proof for WCert
        quality = 0
        scid1_swapped = str(swap_bytes(scid_1))
        proof = mcTest.create_test_proof("sc1", scid1_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant_1, [addr_node2], [bwt_amount])

        mark_logs("Node1 sends a certificate for SC {} using unconfirmed UTXO from tx1".format(scid_1), self.nodes, DEBUG_MODE)
        try:
            cert1 = self.nodes[1].sc_send_certificate(scid_1, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("======> cert1 = {}".format(cert1), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
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
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("{}".format(errorString), self.nodes, DEBUG_MODE)
            assert_true("The transaction was rejected" in errorString)

        # (5) try to do it via a rawtransaction, but that will result by a refusal to be added in mempool
        inputs  = [{'txid' : cert1, 'vout' : 0}]
        change1 = amount1 - amount2 - Decimal("0.0002")
        outputs = { taddr2 : amount2, self.nodes[1].getnewaddress() : change1  }
        mark_logs("Node1 try to do the same using a raw transaction, expecting failure".format(amount2), self.nodes, DEBUG_MODE)
        try:
            rawtx = self.nodes[1].createrawtransaction(inputs, outputs)
            rawtx = self.nodes[1].signrawtransaction(rawtx)
            self.nodes[1].sendrawtransaction(rawtx['hex'])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("{}".format(errorString), self.nodes, DEBUG_MODE)

        # (6) node1 create a cert2 using the same unconfirmed change 
        amounts = []

        #Create proof for WCert
        quality = 0
        scid2_swapped = str(swap_bytes(scid_2))
        proof = mcTest.create_test_proof("sc2", scid2_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant_2, [], [])

        mark_logs("Node1 tries to sends a certificate for SC {} using unconfirmed change from cert1".format(scid_2), self.nodes, DEBUG_MODE)
        try:
            cert2 = self.nodes[1].sc_send_certificate(scid_2, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("======> cert2 = {}".format(cert2), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        # (7) try to do it via a rawcertificate, but that again will result by a refusal to be added in mempool
        mark_logs("Node1 try to do the same using a raw certificate, expecting failure since a certificate for this sc is already in mempool".format(amount2), self.nodes, DEBUG_MODE)
        inputs  = [{'txid' : cert1, 'vout' : 0}]
        change_dum = amount1 - CERT_FEE
        outputs = { self.nodes[1].getnewaddress() : change_dum }
        params = {"scid": scid_2, "quality": quality, "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash, "scProof": proof, "withdrawalEpochNumber": epoch_number}
        try:
            rawcert    = self.nodes[1].createrawcertificate(inputs, outputs, [], params)
            signed_cert = self.nodes[1].signrawtransaction(rawcert)
            #pprint.pprint(self.nodes[1].decoderawtransaction(signed_cert['hex']))
            rawcert = self.nodes[1].sendrawtransaction(signed_cert['hex'])
            assert_true(False)
        except JSONRPCException as e:
            mark_logs("Send certificate failed as expected", self.nodes, DEBUG_MODE)

        self.sync_all()

        mark_logs("Check tx1, cert1 and cert2 are in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, tx1 in self.nodes[2].getrawmempool())
        assert_equal(True, cert1 in self.nodes[2].getrawmempool())
        assert_equal(True, cert2 in self.nodes[2].getrawmempool())
        mp = self.nodes[1].getrawmempool(True)
        #pprint.pprint(mp)

        dep_cert1 = mp[cert1]['depends']
        dep_cert2 = mp[cert2]['depends']

        mark_logs("check that cert1 depends on tx1 {}".format(cert1), self.nodes, DEBUG_MODE)
        assert_true(tx1 in dep_cert1)

        mark_logs("check that cert2 depends on cert1 {}".format(cert1), self.nodes, DEBUG_MODE)
        assert_true(cert1 in dep_cert2)

        mark_logs("Node0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # (8) Node1 send coins to Node2 using confirmed change of cert2
        mark_logs("Node1 sends {} coins to Node2 using confirmed change from cert2".format(amount2), self.nodes, DEBUG_MODE)
        inputs  = [{'txid' : cert2, 'vout' : 0}]
        change2 = change1 - Decimal("0.0002")
        outputs = { taddr2 : amount2, self.nodes[1].getnewaddress() : change2  }
        mark_logs("Node1 try to do the same using a raw transaction, expecting failure".format(amount2), self.nodes, DEBUG_MODE)
        try:
            rawtx = self.nodes[1].createrawtransaction(inputs, outputs)
            rawtx = self.nodes[1].signrawtransaction(rawtx)
            tx2 = self.nodes[1].sendrawtransaction(rawtx['hex'])
            mark_logs("======> tx2 = {}".format(tx2), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("{}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Node0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Check mempools are empty", self.nodes, DEBUG_MODE)
        assert_equal(0, len(self.nodes[0].getrawmempool()))
        assert_equal(0, len(self.nodes[3].getrawmempool()))

        # (9) Node0 invalidates the latest block, tx2 is restored in mempool
        block_inv = self.nodes[0].getbestblockhash()
        mark_logs("Node 0 invalidates latest block with height = {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        self.nodes[0].invalidateblock(block_inv)
        sync_mempools(self.nodes[0:1])
        mp = self.nodes[0].getrawmempool(True)
        pprint.pprint(mp)

        mark_logs("Check tx2 is back in mempool of Node0", self.nodes, DEBUG_MODE)
        assert_equal(True, tx2 in self.nodes[0].getrawmempool())

        # (10) Node0 invalidates one more block, tx1, cert1, cert2 are restored in mempool but tx2 disappears
        # tx2 is removed from mempool because it spends cert2 change that is is now unconfirmed
        block_inv = self.nodes[0].getbestblockhash()
        mark_logs("Node 0 invalidates latest block with height = {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        self.nodes[0].invalidateblock(block_inv)
        sync_mempools(self.nodes[0:1])
        #pprint.pprint(mp)
        mark_logs("Check tx1, cert1 and cert2 are back in mempool of Node0", self.nodes, DEBUG_MODE)
        assert_equal(True, tx1 in self.nodes[0].getrawmempool())
        assert_equal(True, cert1 in self.nodes[0].getrawmempool())
        assert_equal(True, cert2 in self.nodes[0].getrawmempool())

        mark_logs("Check tx2 is no more in mempool of Node0", self.nodes, DEBUG_MODE)
        assert_equal(False, tx2 in self.nodes[0].getrawmempool())

        mark_logs("Node0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        sync_mempools(self.nodes[0:1])

        # (11) Node0 reconsiders last invalidated block in order to allow network alignemnt when other nodes will prevail 
        mark_logs("Node 0 reconsider last invalidated block", self.nodes, DEBUG_MODE)
        self.nodes[0].reconsiderblock(block_inv)
        self.sync_all()

        mark_logs("Check mempools are empty", self.nodes, DEBUG_MODE)
        assert_equal(0, len(self.nodes[0].getrawmempool()))
        assert_equal(0, len(self.nodes[3].getrawmempool()))

        # (12) Node3  generates 2 blocks prevailing over Node0 and realigning the network 
        mark_logs("Node3 generates 2 blocks prevailing over Node0 and realigning the network", self.nodes, DEBUG_MODE)
        self.nodes[3].generate(2)
        self.sync_all()

        # verify network is aligned
        mark_logs("verifying that network is realigned", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[0].getscinfo("*"),     self.nodes[3].getscinfo("*"))
        assert_equal(self.nodes[0].getrawmempool(),    self.nodes[3].getrawmempool())
        assert_equal(self.nodes[0].getblockcount(),    self.nodes[3].getblockcount())
        assert_equal(self.nodes[0].getbestblockhash(), self.nodes[3].getbestblockhash())


if __name__ == '__main__':
    sc_cert_orphans().main()
