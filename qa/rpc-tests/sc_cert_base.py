#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs
import os
from decimal import Decimal
import pprint

DEBUG_MODE = 1
NUMB_OF_NODES = 3
EPOCH_LENGTH = 5
CERT_FEE = 0.0001


class sc_cert_base(BitcoinTestFramework):

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

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        # side chain id
        scid = "1111111111111111111111111111111111111111111111111111111111111111"

        # forward transfer amounts
        creation_amount = Decimal("0.5")
        fwt_amount = Decimal("50")
        bwt_amount_bad = Decimal("100.0")
        bwt_amount = Decimal("50")

        blocks = [self.nodes[0].getblockhash(0)]

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        mark_logs("Node 0 generates 220 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(220))
        self.sync_all()

        # SC creation
        bal_before_sc_creation = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance before SC creation: {}".format(bal_before_sc_creation), self.nodes, DEBUG_MODE)
        creating_tx = self.nodes[1].sc_create(scid, EPOCH_LENGTH, "dada", creation_amount, "abcdef")
        mark_logs("Node 1 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        sc_creating_height = self.nodes[0].getblockcount()
        self.sync_all()

        # Check node 1 balance following sc creation
        fee = self.nodes[1].gettransaction(creating_tx)['fee']
        mark_logs("Fee paid for SC creation: {}".format(fee), self.nodes, DEBUG_MODE)        
        bal_after_sc_creation = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance after SC creation: {}".format(bal_after_sc_creation), self.nodes, DEBUG_MODE)
        assert_equal(bal_before_sc_creation, bal_after_sc_creation + creation_amount - fee)


        # Fwd Transfer to Sc
        bal_before_fwd_tx = self.nodes[0].getbalance("", 0)
        mark_logs("Node0 balance before fwd tx: {}".format(bal_before_fwd_tx), self.nodes, DEBUG_MODE)
        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount, scid)
        mark_logs("Node0 transfers {} coins to SC with tx {}...".format(fwt_amount, fwd_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node0 confirms fwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        # TODO: Check node0 balance following fwd transfert
        print "SC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))
        # mark_logs("Sc {} state: {}".format(scid, self.nodes[0].getscinfo(scid)), self.nodes, DEBUG_MODE)

        mark_logs("Node0 generating 3 more blocks to achieve end of withdrawal epoch", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(3))
        self.sync_all()

        print "SC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))
        mark_logs("Sc {} state: {}".format(scid, self.nodes[0].getscinfo(scid)), self.nodes, DEBUG_MODE)

        current_height = self.nodes[0].getblockcount()
        epoch_number = (current_height - sc_creating_height + 1) // EPOCH_LENGTH - 1
        mark_logs("Current height {}, Sc creation height {}, epoch length {} --> current epoch number {}"
                  .format(current_height, sc_creating_height, EPOCH_LENGTH, epoch_number), self.nodes, DEBUG_MODE)
        epoch_block_hash = self.nodes[0].getblockhash(sc_creating_height - 1 + ((epoch_number + 1) * EPOCH_LENGTH))
        eph_wrong = self.nodes[0].getblockhash(sc_creating_height)
        print "epoch_number = ", epoch_number, ", epoch_block_hash = ", epoch_block_hash

        pkh_node1 = self.nodes[1].getnewaddress("", True)

        mark_logs("Node 0 tries to perform a bwd transfer with insufficient Sc balance...", self.nodes, DEBUG_MODE)
        amounts = [{"pubkeyhash": pkh_node1, "amount": bwt_amount_bad}]

        try:
            cert_bad = self.nodes[0].send_certificate(scid, epoch_number, epoch_block_hash, amounts, CERT_FEE)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("sidechain has insufficient funds" in errorString, True)
        print "SC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))

        mark_logs("Node 0 tries to perform a bwd transfer with an invalid epoch number ...", self.nodes, DEBUG_MODE)
        amount_cert_1 = [{"pubkeyhash": pkh_node1, "amount": bwt_amount}]

        try:
            cert_bad = self.nodes[0].send_certificate(scid, epoch_number + 1, epoch_block_hash, amount_cert_1, CERT_FEE)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("invalid epoch data" in errorString, True)
        print "SC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))

        mark_logs("Node 0 tries to perform a bwd transfer with an invalid end epoch hash block ...", self.nodes, DEBUG_MODE)
        try:
            cert_bad = self.nodes[0].send_certificate(scid, epoch_number, eph_wrong, amount_cert_1, CERT_FEE)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("invalid epoch data" in errorString, True)
        print "SC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))

        mark_logs("Node 0 performs a bwd transfer of {} coins to Node1 pkh".format(amount_cert_1[0]["pubkeyhash"], amount_cert_1[0]["amount"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_0 = self.nodes[0].send_certificate(scid, epoch_number, epoch_block_hash, amount_cert_1, CERT_FEE)
            assert(len(cert_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Checking mempools alignement", self.nodes, DEBUG_MODE)
        self.sync_all()
        for i in range(1, NUMB_OF_NODES):
            assert_equal(sorted(self.nodes[0].getrawmempool()), sorted(self.nodes[i].getrawmempool()))

        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_epoch_0 in self.nodes[0].getrawmempool())

        bal_before_bwt = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance before bwt is received: {}".format(bal_before_bwt), self.nodes, DEBUG_MODE)

        mark_logs("Node 0 try to generate a second bwd transfer for the same epoch number before first bwd is confirmed", self.nodes, DEBUG_MODE)
        try:
            cert_bad = self.nodes[0].send_certificate(scid, epoch_number, epoch_block_hash, amount_cert_1, CERT_FEE)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("invalid cert epoch" in errorString, True)

        print("Node0 confims bwd transfer generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        mark_logs("Node 0 tries to performs a bwd transfer for the same epoch number as before...", self.nodes, DEBUG_MODE)
        try:
            cert_bad = self.nodes[0].send_certificate(scid, epoch_number, epoch_block_hash, amount_cert_1, CERT_FEE)
            print "cert = ", cert_bad
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("invalid cert epoch" in errorString, True)

        mark_logs("Checking that amount transferred by certificate reaches Node1 wallet", self.nodes, DEBUG_MODE)
        retrieved_cert = self.nodes[1].gettransaction(cert_epoch_0)
        assert_equal(retrieved_cert['amount'], 0)        # Certificate amount is not mature yet
        assert_equal(len(retrieved_cert['details']), 0)  # Where the bwt amount be retrieved?

        bal_after_bwt_confirmed = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance after bwt is confirmed: {}".format(bal_after_bwt_confirmed), self.nodes, DEBUG_MODE)
        assert_equal(bal_after_bwt_confirmed, bal_before_bwt) # cert_net_amount is not matured yet. TODO: how to show it?

        mark_logs("Checking that Node1 cannot immediately spend coins received from bwd transfer", self.nodes, DEBUG_MODE)
        mark_logs("Node 1 tries to send {} coins to node2...".format(amount_cert_1[0]["amount"] / 2), self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[1].sendtoaddress(self.nodes[2].getnewaddress(), amount_cert_1[0]["amount"] / 2)
            assert(len(tx) == 0)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("Insufficient funds" in errorString, True)

        mark_logs("Show that coins from bwt can be spent once next epoch certificate is received and confirmed", self.nodes, DEBUG_MODE)
        mark_logs("Node0 generating enough blocks to move to new withdrawal epoch", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(EPOCH_LENGTH-1))
        self.sync_all()

        current_height = self.nodes[0].getblockcount()
        epoch_number = (current_height - sc_creating_height + 1) // EPOCH_LENGTH - 1
        assert_equal(epoch_number, 1)

        mark_logs("Current height {}, Sc creation height {}, epoch length {} --> current epoch number {}"
               .format(current_height, sc_creating_height, EPOCH_LENGTH, epoch_number), self.nodes, DEBUG_MODE)
        epoch_block_hash = self.nodes[0].getblockhash(sc_creating_height - 1 + ((epoch_number + 1) * EPOCH_LENGTH))
        eph_wrong = self.nodes[0].getblockhash(sc_creating_height)
        print "epoch_number = ", epoch_number, ", epoch_block_hash = ", epoch_block_hash
 
        amount_cert_2 = [{"pubkeyhash": pkh_node1, "amount": 0}]

        bal_before_cert_2 = self.nodes[1].getbalance("", 0)
        mark_logs("Generate new certificate for epoch {}. No bwt is included".format(epoch_number), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_1 = self.nodes[0].send_certificate(scid, epoch_number, epoch_block_hash, amount_cert_2, CERT_FEE)
            assert(len(cert_epoch_1) > 0)
            mark_logs("Certificate is {}".format(cert_epoch_1), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Confirm the certificate for epoch {},".format(epoch_number), self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        bal_after_cert_2 = self.nodes[1].getbalance("", 0)    

        mark_logs("Checking that certificate received from previous epoch is spendable,".format(epoch_number), self.nodes, DEBUG_MODE)
        retrieved_cert = self.nodes[1].gettransaction(cert_epoch_0)
        # print "cert for epoch 0, following its maturity:\n", pprint.pprint(retrieved_cert)
        assert_equal(retrieved_cert['amount'],               amount_cert_1[0]["amount"])  # Certificate amount has matured
        assert_equal(retrieved_cert['details'][0]['amount'], amount_cert_1[0]["amount"])  # In cert details you can see the actual amount transferred

        mark_logs("Checking Node1 balance is duly updated,".format(epoch_number), self.nodes, DEBUG_MODE)
        assert_equal(bal_after_cert_2, bal_before_cert_2  + amount_cert_1[0]["amount"])

        Node2_bal_before_cert_expenditure = self.nodes[2].getbalance("", 0)
        mark_logs("Checking that Node1 can spend coins received from bwd transfer in previous epoch", self.nodes, DEBUG_MODE)
        mark_logs("Node 1 sends {} coins to node2...".format(amount_cert_1[0]["amount"] / 2), self.nodes, DEBUG_MODE)
        tx = self.nodes[1].sendtoaddress(self.nodes[2].getnewaddress(), amount_cert_1[0]["amount"] / 2)
        assert(len(tx) > 0)
        print
        vin = self.nodes[1].getrawtransaction(tx, 1)['vin']
        assert_equal(vin[0]['txid'], cert_epoch_0)
 
        mark_logs("Node0 confims spending of bwd transfer founds generating 1 block", self.nodes, DEBUG_MODE)
        self.sync_all()
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        Node2_bal_after_cert_expenditure = self.nodes[2].getbalance("", 0)

        mark_logs("Verify balances following Node1 spending bwd transfer to Node2.", self.nodes, DEBUG_MODE)
        # pprint.pprint(self.nodes[1].gettransaction(tx))
        fee_node2 = self.nodes[1].gettransaction(tx)['fee']
        assert_equal(Node2_bal_before_cert_expenditure + amount_cert_1[0]["amount"] / 2, Node2_bal_after_cert_expenditure)


if __name__ == '__main__':
    sc_cert_base().main()
