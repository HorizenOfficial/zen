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
CERT_FEE = Decimal('0.00015')


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
        bwt_amount = Decimal("0.4")

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates 220 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(220)
        self.sync_all()

        bal1 = self.nodes[1].getbalance()
        print "===> Node1 balance before creating sidechain = ", bal1

        creating_tx = self.nodes[1].sc_create(scid, EPOCH_LENGTH, "dada", creation_amount, "abcdef")
        mark_logs("Node 1 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        sc_creating_height = self.nodes[0].getblockcount()  # Should not this be in SC info??'
        self.sync_all()

        # fee can be seen on sender wallet (it is a negative value)
        fee = self.nodes[1].gettransaction(creating_tx)['fee']
        mark_logs("Fee paid for SC creation: {}".format(fee), self.nodes, DEBUG_MODE)

        mark_logs("Node0 generating 4 more blocks to achieve end of withdrawal epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(4)
        self.sync_all()

        print "SC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))
        mark_logs("Sc {} state: {}".format(scid, self.nodes[0].getscinfo(scid)), self.nodes, DEBUG_MODE)

        current_height = self.nodes[0].getblockcount()
        epoch_number = (current_height - sc_creating_height + 1) // EPOCH_LENGTH - 1
        mark_logs("Current height {}, Sc creation height {}, epoch length {} --> current epoch number {}"
                  .format(current_height, sc_creating_height, EPOCH_LENGTH, epoch_number), self.nodes, DEBUG_MODE)
        epoch_block_hash = self.nodes[0].getblockhash(sc_creating_height - 1 + ((epoch_number + 1) * EPOCH_LENGTH))
        print "epoch_number = ", epoch_number, ", epoch_block_hash = ", epoch_block_hash

        pkh_node2 = self.nodes[2].getnewaddress("", True)

        bal2 = self.nodes[1].getbalance()
        print "===> Node1 balance before sending certificate = ", bal2

        amounts = [{"pubkeyhash": pkh_node2, "amount": bwt_amount}]
        mark_logs("Node 1 performs a bwd transfer of {} coins to Node2 pkh".format(bwt_amount, pkh_node2), self.nodes, DEBUG_MODE)
        try:
            cert_good = self.nodes[1].send_certificate(scid, epoch_number, epoch_block_hash, amounts, CERT_FEE)
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

        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_good in self.nodes[0].getrawmempool())

        print("Node0 confims bwd transfer generating 1 block")
        mined = self.nodes[0].generate(1)[0]
        self.sync_all()

        mark_logs("Check cert is not in mempool anymore", self.nodes, DEBUG_MODE)
        assert_equal(False, cert_good in self.nodes[0].getrawmempool())

        #raw_input("________________")
        bal3 = self.nodes[1].getbalance()
        print "===> Node1 balance after sending certificate = ", bal3

        mark_logs("Check block coinbase contains the certificate fee", self.nodes, DEBUG_MODE)
        coinbase = self.nodes[0].getblock(mined, True)['tx'][0]
        decoded_coinbase = self.nodes[2].getrawtransaction(coinbase, 1)
        miner_quota = decoded_coinbase['vout'][0]['value']
        assert_equal(miner_quota, (Decimal('7.5') + CERT_FEE))

        mark_logs("Checking that amount transferred by certificate reaches Node2 wallet", self.nodes, DEBUG_MODE)
        cert_net_amount = self.nodes[2].gettransaction(cert_good)['amount']
        assert_equal(cert_net_amount, bwt_amount)


if __name__ == '__main__':
    sc_cert_base().main()
