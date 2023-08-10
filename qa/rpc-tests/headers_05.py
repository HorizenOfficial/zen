#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import initialize_chain_clean, start_nodes, connect_nodes_bi, \
    assert_equal, mark_logs, sync_blocks, colorize as cc
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

        blocks.extend(self.nodes[1].generate(7)) # block height 2--8
        mark_logs("Node 1 generated 7 honest blocks: ", self.nodes, DEBUG_MODE, color='c')
        blA = blocks[2]
        blB = blocks[4]
        blC = blocks[6]
        self.sync_all()

        blocks.extend(self.nodes[2].generate(20)) # block height 2--21
        mark_logs("Node 2 generated 20 mal blocks:    ", self.nodes, DEBUG_MODE, color='c')
        self.sync_all()

        print_ordered_tips(self.nodes)
# Node(0): [0]->[1]->[2h]->..->[8h]
#   |                   
# Node(1): [0]->[1]->[2h]->..->[8h]
#                       
# Node(2): [0]->[1]->[2m]---------->[21m]

        mark_logs("Join network (1)--------(2)", self.nodes, DEBUG_MODE, color='b')
        connect_nodes_bi(self.nodes, 1, 2)  # They are not going to sync their blocks, so do not use join_network()
        self.is_network_split = False
        sync_blocks(self.nodes, 1, False, 5)
        mark_logs("The network has joined", self.nodes, DEBUG_MODE, color='e')

        print_ordered_tips(self.nodes)
# Node(0): [0]->[1]->[2h]---->[8h]         **Active**
#   |             \     
#   |              +->[2m]--------->[21m]
#   |                   
# Node(1): [0]->[1]->[2h]---->[8h]         **Active**
#   |             \     
#   |              +->[2m]--------->[21m]
#   |                   
# Node(2): [0]->[1]->[2m]---------->[21m]  **Active**
#                 \     
#                  +->[2h]--->[8h]

        mark_logs(f"Checking finality of block {blA} at height 2", self.nodes, DEBUG_MODE, color='g')
        get_block_finality(self.nodes, 0, blA, 8)
        get_block_finality(self.nodes, 1, blA, 8)
        mark_logs(f"Checking finality of block {blB} at height 4", self.nodes, DEBUG_MODE, color='g')
        get_block_finality(self.nodes, 0, blB, 6)
        get_block_finality(self.nodes, 1, blB, 6)
        mark_logs(f"Checking finality of block {blC} at height 6", self.nodes, DEBUG_MODE, color='g')
        get_block_finality(self.nodes, 0, blC, 4)
        get_block_finality(self.nodes, 1, blC, 4)

        ex = [              # Expected block finalities per iteration
            [7, 6, 4],
            [6, 6, 4],
            [5, 5, 4],
            [4, 4, 4],
            [3, 3, 3],
            [2, 2, 2],
            [1, 1, 1],
            [-1, -1, -1]       # Dummy value, this should not be reached
        ]

        for j in range(1, 10):
            mark_logs(f"### Block {j}", self.nodes, DEBUG_MODE, color='e')
            blocks.extend(self.nodes[2].generate(1)) # block height 8--12 across the iterations
            mark_logs(cc('c', "Node 2 generated a malicious block: ") + blocks[len(blocks)-1], self.nodes, DEBUG_MODE, color='n')
            sync_blocks(self.nodes, 1, False, 3)

            try:
                mark_logs(f"Checking finality of block {blA} at height 2", self.nodes, DEBUG_MODE, color='g')
                get_block_finality(self.nodes, 0, blA, ex[j-1][0])
                get_block_finality(self.nodes, 1, blA, ex[j-1][0])
                mark_logs(f"Checking finality of block {blB} at height 4", self.nodes, DEBUG_MODE, color='g')
                get_block_finality(self.nodes, 0, blB, ex[j-1][1])
                get_block_finality(self.nodes, 1, blB, ex[j-1][1])
                mark_logs(f"Checking finality of block {blC} at height 6", self.nodes, DEBUG_MODE, color='g')
                get_block_finality(self.nodes, 0, blC, ex[j-1][2])
                get_block_finality(self.nodes, 1, blC, ex[j-1][2])
                print()
            except JSONRPCException as e:
                errorString = e.error['message']
                mark_logs(errorString, self.nodes, DEBUG_MODE, color='y')
                mark_logs(f"===> Malicious attach succeeded after {j} blocks!", self.nodes, DEBUG_MODE, color='y')
                break

        print_ordered_tips(self.nodes)
# Node(0): [0]->[1]->[2h]---->[8h]
#   |             \     
#   |              +->[2m]--------->[29m]  **Active**
#   |                   
# Node(1): [0]->[1]->[2h]---->[8h]
#   |             \     
#   |              +->[2m]--------->[29m]  **Active**
#   |                   
# Node(2): [0]->[1]->[2m]---------->[29m]  **Active**
#                 \     
#                  +->[2h]--->[8h]

        mark_logs("Check that all the nodes have the same active tip", self.nodes, DEBUG_MODE, color='g')
        node0ActiveTip = [tip for tip in self.nodes[0].getchaintips() if tip['status'] == 'active'][0]
        node1ActiveTip = [tip for tip in self.nodes[1].getchaintips() if tip['status'] == 'active'][0]
        node2ActiveTip = [tip for tip in self.nodes[2].getchaintips() if tip['status'] == 'active'][0]
        assert_equal(node0ActiveTip['hash'], blocks[len(blocks)-1])
        assert_equal(node1ActiveTip['hash'], blocks[len(blocks)-1])
        assert_equal(node2ActiveTip['hash'], blocks[len(blocks)-1])


if __name__ == '__main__':
    headers().main()
