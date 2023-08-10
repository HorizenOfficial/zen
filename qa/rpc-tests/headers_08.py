#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import initialize_chain_clean, start_nodes, mark_logs, \
    sync_blocks, connect_nodes_bi, assert_equal, colorize as cc
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

        mark_logs("Split nodes (1)----x   x---(2)", self.nodes, DEBUG_MODE, color='b')
        self.split_network(1)
        mark_logs("The network is split", self.nodes, DEBUG_MODE, color='e')

        blocks.extend(self.nodes[1].generate(19)) # block height 20
        mark_logs("Node 1 generated 19 honest block: ", self.nodes, DEBUG_MODE, color='c')
        bl2 = blocks[2]
        self.sync_all()

        blocks.extend(self.nodes[2].generate(20)) # block height 21
        mark_logs("Node 2 generated 20 mal block: ", self.nodes, DEBUG_MODE, color='c')
        self.sync_all()

        print_ordered_tips(self.nodes)
# Node(0): [0]->[1]->[20h]
#   |                   
# Node(1): [0]->[1]->[20h]
#                       
# Node(2): [0]->[1]->[21m]

        mark_logs("Join network (1)--------(2)", self.nodes, DEBUG_MODE, color='b')
        connect_nodes_bi(self.nodes, 1, 2)      # Chain will not syncronize, do not use join_network()
        sync_blocks(self.nodes, 1, False, 5)
        mark_logs("The network has joined", self.nodes, DEBUG_MODE, color='e')

        print_ordered_tips(self.nodes)
# Node(0): [0]->[1]->[20h]  **Active**
#   |             \     
#   |              +->[21m]
#   |                   
# Node(1): [0]->[1]->[20h]  **Active**
#   |             \     
#   |              +->[21m]
#   |                   
# Node(2): [0]->[1]->[21m]  **Active**
#                 \     
#                  +->[20h]

        try:
            mark_logs(f"Checking finality of block {bl2}", self.nodes, DEBUG_MODE, color='g')
            get_block_finality(self.nodes, 0, bl2, 170)
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)

        blocks.extend(self.nodes[2].generate(170)) # block height 191
        mark_logs("Node 2 generated 170 mal blocks: ", self.nodes, DEBUG_MODE, color='c')
        sync_blocks(self.nodes, 1, True, 3)

        print_ordered_tips(self.nodes)
# Node(0): [0]->[1]->[20h]
#   |             \     
#   |              +->[191m]  **Active**
#   |                   
# Node(1): [0]->[1]->[20h]
#   |             \     
#   |              +->[191m]  **Active**
#   |                   
# Node(2): [0]->[1]->[191m]  **Active**
#                 \     
#                  +->[20h]

        mark_logs("Check that all nodes have the same active tip", self.nodes, DEBUG_MODE, color='g')
        node0ActiveTip = [tip for tip in self.nodes[0].getchaintips() if tip['status'] == 'active'][0]
        node1ActiveTip = [tip for tip in self.nodes[1].getchaintips() if tip['status'] == 'active'][0]
        node2ActiveTip = [tip for tip in self.nodes[2].getchaintips() if tip['status'] == 'active'][0]
        assert_equal(node0ActiveTip['hash'], blocks[len(blocks)-1])
        assert_equal(node1ActiveTip['hash'], blocks[len(blocks)-1])
        assert_equal(node2ActiveTip['hash'], blocks[len(blocks)-1])


if __name__ == '__main__':
    headers().main()
