#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import pprint
import time
from decimal import Decimal

from test_framework.authproxy import JSONRPCException
from test_framework.mc_test.mc_test import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.util import initialize_chain_clean, \
    sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, assert_true, assert_false, start_node, \
    stop_nodes, wait_bitcoinds, assert_equal

DEBUG_MODE = 1
NUMB_OF_NODES = 3
EPOCH_LENGTH = 5
OPT_MAX_BLOCK_HEIGHT1 = 450
OPT_MAX_BLOCK_HEIGHT2 = 440

def local_sync_blocks(node, counts, wait=1, p=False, limit_loop=0):
    """
    Wait until everybody has the same block count or a limit has been exceeded
    """
    loop_num = 0
    while True:
        if limit_loop > 0:
            loop_num += 1
            if loop_num > limit_loop:
                break
        n = node.getblockcount()
        if p :
            print(n)
        if counts <= n:
            break
        time.sleep(wait)

class MaxHeightForAlign(BitcoinTestFramework):

    def sync_all_but_node0(self):
        sync_blocks(self.nodes[1:])
        sync_blocks(self.nodes[0:1])
        sync_mempools(self.nodes)


    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES+1)

    def setup_network(self, split=False):
        self.nodes = []

        for i in range(0, NUMB_OF_NODES):
            if i > 0:
                self.nodes += [start_node(i, self.options.tmpdir,
                                          extra_args=['-debug=py', '-debug=mempool', '-logtimemicros=1'])]
            else:
                # in node 0 set a maximum height for alignment
                self.nodes += [start_node(i, self.options.tmpdir,
                                          extra_args=['-debug=py', '-debug=mempool', '-logtimemicros=1',
                                                      '-maxblockheightforalign=%d' % OPT_MAX_BLOCK_HEIGHT1])]
        for k in range(0, NUMB_OF_NODES - 1):
            connect_nodes_bi(self.nodes, k, k + 1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all_but_node0()

    def run_test(self):
        """
        Test description: TODO
        """

        mark_logs("Node 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Node 2 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[2].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all_but_node0()

        mark_logs("Node 1 generates {} blocks".format(49), self.nodes, DEBUG_MODE)
        self.nodes[2].generate(49)
        self.sync_all_but_node0()
        time.sleep(3)

        current_height_0 = self.nodes[0].getblockcount()
        current_height_1 = self.nodes[1].getblockcount()
        current_height_2 = self.nodes[2].getblockcount()

        mark_logs("Node 0 height={}".format(current_height_0), self.nodes, DEBUG_MODE)
        mark_logs("Node 1 height={}".format(current_height_1), self.nodes, DEBUG_MODE)
        mark_logs("Node 2 height={}".format(current_height_2), self.nodes, DEBUG_MODE)

        assert_equal(OPT_MAX_BLOCK_HEIGHT1, current_height_0)
        assert_equal(current_height_1, current_height_2)
        assert_true(current_height_1 > OPT_MAX_BLOCK_HEIGHT1)

        num_of_conn = len(self.nodes[0].getpeerinfo())
        mark_logs("Node 0 has still {} connections to its peer".format(num_of_conn), self.nodes, DEBUG_MODE)
        assert_equal(num_of_conn, 2, "Unexpected num of conn to node 1")

        mark_logs("...stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)
        time.sleep(3)

        current_height_0 = self.nodes[0].getblockcount()
        current_height_1 = self.nodes[1].getblockcount()
        current_height_2 = self.nodes[2].getblockcount()

        mark_logs("Node 0 height={}".format(current_height_0), self.nodes, DEBUG_MODE)
        mark_logs("Node 1 height={}".format(current_height_1), self.nodes, DEBUG_MODE)
        mark_logs("Node 2 height={}".format(current_height_2), self.nodes, DEBUG_MODE)

        assert_equal(OPT_MAX_BLOCK_HEIGHT1, current_height_0)
        assert_equal(current_height_1, current_height_2)
        assert_true(current_height_1 > OPT_MAX_BLOCK_HEIGHT1)

        # Connect a fifth node from scratch and update
        s = "Connecting a new node with a different max block height for alignment"
        mark_logs(s, self.nodes, DEBUG_MODE)
        self.nodes.append(start_node(NUMB_OF_NODES, self.options.tmpdir,
                                     extra_args=['-debug=py', '-debug=mempool', '-logtimemicros=1',
                                                 '-maxblockheightforalign=%d' % OPT_MAX_BLOCK_HEIGHT2] ))

        #  Node 3 is connected to node 0, can not go beyond is height:
        #     3 <-> 0 <-> 1 <-> 2
        assert_true(OPT_MAX_BLOCK_HEIGHT1 > OPT_MAX_BLOCK_HEIGHT2)

        connect_nodes_bi(self.nodes, NUMB_OF_NODES, 0)
        local_sync_blocks(self.nodes[NUMB_OF_NODES], OPT_MAX_BLOCK_HEIGHT2)
        time.sleep(3)

        current_height_3 = self.nodes[3].getblockcount()
        mark_logs("Node 3 height={}".format(current_height_3), self.nodes, DEBUG_MODE)
        assert_equal(OPT_MAX_BLOCK_HEIGHT2, current_height_3)

        # check we can propagate tx to all mempools even if block heights are different
        tAddr = self.nodes[1].getnewaddress()
        self.nodes[0].sendtoaddress(tAddr, 1.0)
        sync_mempools(self.nodes)

        mpsz = len(self.nodes[0].getrawmempool())
        mark_logs("Node {} mempool size={}".format(0, mpsz), self.nodes, DEBUG_MODE)
        for i in range(1, len(self.nodes)):
            mark_logs("Node {} mempool size={}".format(i, mpsz), self.nodes, DEBUG_MODE)
            assert_equal(mpsz, len(self.nodes[i].getrawmempool()))


if __name__ == '__main__':
    MaxHeightForAlign().main()
