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
import time

DEBUG_MODE = 1
NUMB_OF_NODES = 4
EPOCH_LENGTH = 5
CERT_FEE = Decimal('0.00015')


class sc_cert_change(BitcoinTestFramework):

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
        1) node0 create sidechain with 10.0 coins
           reach epoch 0
        2) node0 create a cert_ep0 for funding node1 1.0 coins
           reach epoch 1
        3) node0 create a cert_ep1 for funding node2 2.0 coins
           reach epoch 2
        4) node1 create a cert_ep2 for funding node3 3.0 coins: he uses cert_ep0 as input, change will be obtained out of a fee=0.0001
           node0 mine a new block
        5) node1 has just one UTXO with the change of cert_ep0 and it should be 0.999, it sends 0.5 to node3
           node0 mine a new block
        6) node3 has 0.5 balance and 3.0 immature from cert_ep2
        '''
        # side chain id
        scid = "1111111111111111111111111111111111111111111111111111111111111111"

        # cross chain transfer amounts
        creation_amount = Decimal("10.0")

        mark_logs("Node 0 generates 220 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(220)
        self.sync_all()

        # (1) node0 create sidechain with 10.0 coins
        creating_tx = self.nodes[0].sc_create(scid, EPOCH_LENGTH, "dada", creation_amount, "abcdef")
        mark_logs("Node 0 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node0 generates 5 blocks to achieve end of epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()
        epoch_block_hash, epoch_number = self.getEpochData(scid);

        # (2) node0 create a cert_ep0 for funding node1 1.0 coins
        pkh_node1 = self.nodes[1].getnewaddress("", True)
        bwt_amount = Decimal("1.0")
        amounts = [{"pubkeyhash": pkh_node1, "amount": bwt_amount}]
        mark_logs("Node 0 performs a bwd transfer of {} coins to Node1 pkh".format(bwt_amount, pkh_node1), self.nodes, DEBUG_MODE)
        try:
            cert_ep0 = self.nodes[0].send_certificate(scid, epoch_number, epoch_block_hash, amounts, CERT_FEE)
            assert(len(cert_ep0) > 0)
            mark_logs("Certificate is {}".format(cert_ep0), self.nodes, DEBUG_MODE)
            self.sync_all()
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Node0 generates 5 blocks to achieve end of epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()
        epoch_block_hash, epoch_number = self.getEpochData(scid);

        # (3) node0 create a cert_ep1 for funding node1 1.0 coins
        pkh_node2 = self.nodes[2].getnewaddress("", True)
        bwt_amount = Decimal("2.0")
        amounts = [{"pubkeyhash": pkh_node2, "amount": bwt_amount}]
        mark_logs("Node 0 performs a bwd transfer of {} coins to Node2 pkh".format(bwt_amount, pkh_node2), self.nodes, DEBUG_MODE)
        try:
            cert_ep1 = self.nodes[0].send_certificate(scid, epoch_number, epoch_block_hash, amounts, CERT_FEE)
            assert(len(cert_ep1) > 0)
            mark_logs("Certificate is {}".format(cert_ep1), self.nodes, DEBUG_MODE)
            self.sync_all()
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Node0 generates 5 blocks to achieve end of epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()
        epoch_block_hash, epoch_number = self.getEpochData(scid);

        # (4) node1 create a cert_ep2 for funding node3 3.0 coins: he uses cert_ep0 as input, change will be obtained out of a fee=0.0001
        pkh_node3 = self.nodes[3].getnewaddress("", True)
        bwt_amount = Decimal("3.0")
        amounts = [{"pubkeyhash": pkh_node3, "amount": bwt_amount}]
        mark_logs("Node 1 performs a bwd transfer of {} coins to Node3 pkh".format(bwt_amount, pkh_node3), self.nodes, DEBUG_MODE)
        try:
            cert_ep2 = self.nodes[1].send_certificate(scid, epoch_number, epoch_block_hash, amounts, CERT_FEE)
            assert(len(cert_ep2) > 0)
            mark_logs("Certificate is {}".format(cert_ep2), self.nodes, DEBUG_MODE)
            self.sync_all()
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Node0 generates 1 blocks", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # (5) node1 has just one UTXO with the change of cert_ep2 and it should be 0.999, it sends 0.5 to node3
        utxos = self.nodes[1].listunspent()
        # pprint.pprint(utxos)
        assert_equal(len(utxos), 1)
        assert_equal(utxos[0]['txid'], cert_ep2)

        taddr3 = self.nodes[3].getnewaddress()
        amount3 = Decimal("0.5")
        self.nodes[0].sendtoaddress(taddr3, amount3)
        self.sync_all()

        mark_logs("Node0 generates 1 blocks", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # (6) node3 has 0.5 balance and 3.0 immature from cert_ep2
        # the only contribution to node3 balance is the transparent fund just received
        assert_equal(self.nodes[3].getbalance(), amount3)

        res = self.nodes[3].gettransaction(cert_ep2)
        # node3 has also immature amounts deriving from the latest certificate whose change has been used for the funds just received 
        # pprint.pprint(res)
        assert_equal(res['amount'], 0.0)
        assert_equal(res['details'][0]['amount'], bwt_amount)
        assert_equal(res['txid'], cert_ep2)


if __name__ == '__main__':
    sc_cert_change().main()
