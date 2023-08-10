#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import initialize_chain_clean, start_nodes, mark_logs, \
    assert_equal, colorize as cc
from headers_common import print_ordered_tips, get_block_finality

DEBUG_MODE = 1

class headers(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_nodes(self):
        self.nodes = start_nodes(3, self.options.tmpdir)

    def run_test(self):
        blocks = []

        blocks.append(self.nodes[0].getblockhash(0))
        mark_logs(cc('e', "Genesis block is:         ") + blocks[0], self.nodes, DEBUG_MODE, color='n')

        blocks.extend(self.nodes[1].generate(1)) # block height 1
        mark_logs(cc('c', "Node 1 generated a block: ") + blocks[len(blocks)-1], self.nodes, DEBUG_MODE, color='n')
        self.sync_all()

        print_ordered_tips(self.nodes)
# Node(0): [0]->[1]
#   |
# Node(1): [0]->[1]
#   |
# Node(2): [0]->[1]

        mark_logs("Split network", self.nodes, DEBUG_MODE, color='b')
        self.split_network(1)
        mark_logs("The network is split", self.nodes, DEBUG_MODE, color='e')

        blocks.extend(self.nodes[1].generate(1)) # block height 2
        mark_logs(cc('c', "Node 1 generated 1 honest block: ") + blocks[2], self.nodes, DEBUG_MODE, color='n')
        self.sync_all()

        blocks.extend(self.nodes[2].generate(1)) # block height 2
        mark_logs(cc('c', "Node 2 generated 1 mal block:    ") + blocks[3], self.nodes, DEBUG_MODE, color='n')
        self.sync_all()

        print_ordered_tips(self.nodes)
# Node(0): [0]->[1]->[2h]
#   |
# Node(1): [0]->[1]->[2h]
#
# Node(2): [0]->[1]->[2m]

        mark_logs("Join network", self.nodes, DEBUG_MODE, color='b')
        self.join_network()
        mark_logs("The network has joined", self.nodes, DEBUG_MODE, color='e')

        print_ordered_tips(self.nodes)
# Node(0): [0]->[1]->[2h]  **Active**
#   |
# Node(1): [0]->[1]->[2h]  **Active**
#   |             \
#   |              +->[2m]
#   |
# Node(2): [0]->[1]->[2m]  **Active**
#                 \
#                  +->[2h]

        block_hash = blocks[2]
        mark_logs(f"Checking finality of block {block_hash}", self.nodes, DEBUG_MODE, color='g')
        get_block_finality(self.nodes, 0, block_hash, 2)
        get_block_finality(self.nodes, 1, block_hash, 1)

        blocks.extend(self.nodes[2].generate(1)) # block height 3
        mark_logs(cc('c', "Node 2 generated 1 mal block:    ") + blocks[4], self.nodes, DEBUG_MODE, color='n')
        mark_logs("Syncing network after malicious attack", self.nodes, DEBUG_MODE, color='c')
        self.sync_all()

        print_ordered_tips(self.nodes)
# Node(0): [0]->[1]->[2h]
#   |             \
#   |              +->[2m]->[3m]  **Active**
#   |
# Node(1): [0]->[1]->[2h]
#   |             \
#   |              +->[2m]->[3m]  **Active**
#   |
# Node(2): [0]->[1]->[2m]->[3m]  **Active**
#                 \
#                  +->[2h]

        mark_logs("Check that nodes are sync'ed - same height", self.nodes, DEBUG_MODE, color='g')
        assert_equal(self.nodes[0].getblockcount(), 3)
        assert_equal(self.nodes[1].getblockcount(), 3)
        assert_equal(self.nodes[2].getblockcount(), 3)

        mark_logs("Check that all the nodes have the same active tip", self.nodes, DEBUG_MODE, color='g')
        node0ActiveTip = [tip for tip in self.nodes[0].getchaintips() if tip['status'] == 'active'][0]
        node1ActiveTip = [tip for tip in self.nodes[1].getchaintips() if tip['status'] == 'active'][0]
        node2ActiveTip = [tip for tip in self.nodes[2].getchaintips() if tip['status'] == 'active'][0]
        assert_equal(node0ActiveTip['hash'], blocks[4])
        assert_equal(node1ActiveTip['hash'], blocks[4])
        assert_equal(node2ActiveTip['hash'], blocks[4])


if __name__ == '__main__':
    headers().main()
