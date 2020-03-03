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

        bal_before = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance before SC creation: {}".format(bal_before), self.nodes, DEBUG_MODE)

        creating_tx = self.nodes[1].sc_create(scid, EPOCH_LENGTH, "dada", creation_amount, "abcdef")
        mark_logs("Node 1 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        sc_creating_height = self.nodes[0].getblockcount()  # Should not this be in SC info??'
        self.sync_all()

        # fee can be seen on sender wallet (it is a negative value)
        fee = self.nodes[1].gettransaction(creating_tx)['fee']
        mark_logs("Fee paid for SC creation: {}".format(fee), self.nodes, DEBUG_MODE)

        # node 1 has just the coinbase minus the sc creation amount
        assert_equal(bal_before, self.nodes[1].getbalance("", 0) + creation_amount - fee)
        mark_logs("Node1 balance after SC creation: {}".format(self.nodes[1].getbalance("", 0)), self.nodes, DEBUG_MODE)

        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount, scid)
        mark_logs("Node 0 transfers {} coins to SC with tx {}...".format(fwt_amount, fwd_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node0 confirms fwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        print "SC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))
        mark_logs("Sc {} state: {}".format(scid, self.nodes[0].getscinfo(scid)), self.nodes, DEBUG_MODE)

        mark_logs("Node0 generating 3 more blocks to achieve end of withdrawal epoch", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(3))
        self.sync_all()

        print "SC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))
        mark_logs("Sc {} state: {}".format(scid, self.nodes[0].getscinfo(scid)), self.nodes, DEBUG_MODE)

#         mark_logs("Node0 generating 1 more blocks to achieve end epoch", self.nodes, DEBUG_MODE)
#         blocks.extend(self.nodes[0].generate(1))
#         self.sync_all()
#
#         print "SC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))
#         mark_logs("Sc {} state: {}".format(scid, self.nodes[0].getscinfo(scid)), self.nodes, DEBUG_MODE)

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
            cert_bad = self.nodes[0].send_certificate(scid, epoch_number, epoch_block_hash, amounts)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("sidechain has insufficient funds" in errorString, True)
        print "SC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))

        mark_logs("Node 0 tries to perform a bwd transfer with an invalid epoch number ...", self.nodes, DEBUG_MODE)
        amounts = [{"pubkeyhash": pkh_node1, "amount": bwt_amount}]

        try:
            cert_bad = self.nodes[0].send_certificate(scid, epoch_number + 1, epoch_block_hash, amounts)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("invalid epoch data" in errorString, True)
        print "SC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))

        mark_logs("Node 0 tries to perform a bwd transfer with an invalid end epoch hash block ...", self.nodes, DEBUG_MODE)
        # check this is refused because end epoch block hash is wrong
        try:
            cert_bad = self.nodes[0].send_certificate(scid, epoch_number, eph_wrong, amounts)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("invalid epoch data" in errorString, True)
        print "SC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))

        mark_logs("Node 0 performs a bwd transfer of {} coins to Node1 pkh".format(bwt_amount, pkh_node1), self.nodes, DEBUG_MODE)
        try:
            cert_good = self.nodes[0].send_certificate(scid, epoch_number, epoch_block_hash, amounts)
            assert(len(cert_good) > 0)
            mark_logs("Certificate is {}".format(cert_good), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Checking mempools alignement", self.nodes, DEBUG_MODE)
        self.sync_all()
        for i in range(1, NUMB_OF_NODES):
            assert_equal(sorted(self.nodes[0].getrawmempool()), sorted(self.nodes[i].getrawmempool()))

        bal_before = self.nodes[1].getbalance("", 0)
        # print "Node1 balance: ", bal_before

        mark_logs("Node 0 try to generate a second bwd transfer for the same epoch number before first bwd is confirmed", self.nodes, DEBUG_MODE)
        try:
            cert_bad = self.nodes[0].send_certificate(scid, epoch_number, epoch_block_hash, amounts)
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
            cert_bad = self.nodes[0].send_certificate(scid, epoch_number, epoch_block_hash, amounts)
            print "cert = ", cert_bad
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("invalid cert epoch" in errorString, True)

        mark_logs("Checking that amount transferred by certificate reaches Node1 wallet", self.nodes, DEBUG_MODE)
        cert_net_amount = self.nodes[1].gettransaction(cert_good)['amount']
        bal_after = self.nodes[1].getbalance("", 0)
        assert_equal(bal_after, bal_before + cert_net_amount)

        bal_before = bal_after

        mark_logs("Checking that Node1 can spend coins received from bwd transfer", self.nodes, DEBUG_MODE)
        mark_logs("Node 1 sends {} coins to node2...".format(bwt_amount / 2), self.nodes, DEBUG_MODE)
        tx = self.nodes[1].sendtoaddress(self.nodes[2].getnewaddress(), bwt_amount / 2)
        assert(len(tx) > 0)
        vin = self.nodes[1].getrawtransaction(tx, 1)['vin']
        assert_equal(vin[0]['txid'], cert_good)

        mark_logs("Node0 confims spending of bwd transfer founds generating 1 block", self.nodes, DEBUG_MODE)
        self.sync_all()
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        mark_logs("Verify balances following Node1 spending bwd transfer to Node2.", self.nodes, DEBUG_MODE)
        bal_after = self.nodes[1].getbalance("", 0)
        fee_node2 = self.nodes[1].gettransaction(tx)['fee']
        assert_equal(bal_after, bal_before - (bwt_amount / 2) + fee_node2)
        assert_equal(self.nodes[2].getbalance("", 0), (bwt_amount / 2))


if __name__ == '__main__':
    sc_cert_base().main()
