#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Exercise the getchaintips API.  We introduce a network split, work
# on chains of different lengths, and join the network together again.
# This gives us two tips, verify that it works.
import time
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, stop_nodes, wait_bitcoinds, \
    sync_blocks, sync_mempools, wait_bitcoinds, \
    connect_nodes_bi

class GetChainTipsTest (BitcoinTestFramework):

    def setup_network(self, split = False):
        self.setup_nodes()

        # Connect the nodes as a "chain".  This allows us
        # to split the network between nodes 1 and 2 to get
        # two halves that can work on competing chains.
        # If we joined network halves, connect the nodes from the joint
        # on outward.  This ensures that chains are properly reorganised.
        if not split:
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:3])
            sync_mempools(self.nodes[1:3])
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 2, 3)
        self.is_network_split = split
        self.sync_all()
       
    def split_network(self):
        """
        Split the network of four nodes into nodes 0/1 and 2/3.
        """
        assert not self.is_network_split
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(True)

    def join_network(self):
        """
        Join the (previously split) network halves together.
        """
        assert self.is_network_split
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

    def run_test (self):
        BitcoinTestFramework.run_test (self)

        tips = self.nodes[0].getchaintips ()
        assert_equal (len (tips), 1)
        assert_equal (tips[0]['branchlen'], 0)
        assert_equal (tips[0]['height'], 200)
        assert_equal (tips[0]['status'], 'active')

        # Split the network and build two chains of different lengths.
        self.split_network ()
        self.nodes[0].generate(10)
        self.nodes[2].generate(20)

        self.sync_all ()

        tips = self.nodes[1].getchaintips ()
        assert_equal (len (tips), 1)
        shortTip = tips[0]
        assert_equal (shortTip['branchlen'], 0)
        assert_equal (shortTip['height'], 210)
        assert_equal (tips[0]['status'], 'active')

        tips = self.nodes[3].getchaintips ()
        assert_equal (len (tips), 1)
        longTip = tips[0]
        assert_equal (longTip['branchlen'], 0)
        assert_equal (longTip['height'], 220)
        assert_equal (tips[0]['status'], 'active')

        # Join the network halves and check that we now have two tips
        # (at least at the nodes that previously had the short chain).
        self.join_network()
        time.sleep(2)

        tips = self.nodes[0].getchaintips ()
        assert_equal (len (tips), 2)
        assert_equal (tips[0], longTip)

        assert_equal (tips[1]['branchlen'], 10)
        assert_equal (tips[1]['status'], 'valid-fork')
        tips[1]['branchlen'] = 0
        tips[1]['status'] = 'active'
        assert_equal (tips[1], shortTip)

if __name__ == '__main__':
    GetChainTipsTest ().main ()
