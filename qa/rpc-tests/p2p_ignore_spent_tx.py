#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs,\
    wait_and_assert_operationid_status
import os
import time
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 3
FEE = Decimal("0.0001")

class old_tx_ignored(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=mempool', '-debug=net', '-forcelocalban']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.sync_all()

    def run_test(self):
        '''
        This test checks that nodes do not ask for tx that they already received in the past, if such tx have been already spent.
        Such scenario not only incurs unnecessary overhead, with extra p2p messages being sent for no reason,
        but may even lead to honest nodes being banned, in case a tx is re-sent after a fork change.
        This is a corner case which should be extremely unlikely in real world, but reproducible in tests.

        CHECKLIST:
        - when multiple peers send inv for a tx, make sure that even if tx is spent after it has been received, later
        requests for the same tx are blocked before any p2p msg is sent
        '''

        z_addr_node0 = self.nodes[0].z_getnewaddress()
        z_addr_node2 = self.nodes[2].z_getnewaddress()

        mark_logs("Node0 generates {} blocks".format(101), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(101)
        self.sync_all()

        mark_logs("Node0 creates and confirms tx0", self.nodes, DEBUG_MODE)
        opid = self.nodes[0].z_shieldcoinbase("*", z_addr_node0)['opid']
        txid = wait_and_assert_operationid_status(self.nodes[0], opid)
        mark_logs("tx0_id: {}".format(txid), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Node1 and Node2 receive inv for tx0 from their peers, scheduling getdata at times now and now+2min", self.nodes, DEBUG_MODE)

        mark_logs("Node0 creates another tx (to Node2) to spend tx0 completely", self.nodes, DEBUG_MODE)
        available = self.nodes[0].z_getbalance(z_addr_node0)
        print(available)
        opid = self.nodes[0].z_sendmany(z_addr_node0, [{"address": z_addr_node2, "amount": available-FEE}], 1, FEE)
        wait_and_assert_operationid_status(self.nodes[0], opid)
        self.sync_all()
        
        mark_logs("Node0 generates {} blocks to reach GROTH height".format(100), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(100)
        self.sync_all()

        mark_logs("Sleep for 2min to let Node1 and Node2 reach the second scheduled event to ask for tx0 data", self.nodes, DEBUG_MODE)
        time.sleep(120)

        mark_logs("Check ban scores", self.nodes, DEBUG_MODE)
        for p in self.nodes[1].getpeerinfo():
            assert_equal(p['banscore'], 0)

if __name__ == '__main__':
    old_tx_ignored().main()

