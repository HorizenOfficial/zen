#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import initialize_chain_clean, start_nodes, mark_logs, \
        assert_equal, sync_blocks, sync_mempools, connect_nodes_bi, disconnect_nodes, \
        colorize as cc
from headers_common import print_ordered_tips, get_block_finality

DEBUG_MODE = 1

class headers(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4)

    def setup_network(self, split=False):
        self.nodes = []
        self.nodes = start_nodes(4, self.options.tmpdir)

        if not split:
            # 1 and 2 are joint only if split==false
            connect_nodes_bi(self.nodes, 1, 2)
            connect_nodes_bi(self.nodes, 1, 3)
            sync_blocks(self.nodes[1:4])
            sync_mempools(self.nodes[1:4])

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def split_network_2(self):
        disconnect_nodes(self.nodes, 1, 3)
        disconnect_nodes(self.nodes, 3, 1)

    def join_network_2(self):
        connect_nodes_bi(self.nodes, 1, 3)
        sync_blocks([self.nodes[1], self.nodes[3]])

    def run_test(self):
        '''
               (3)
               /
        (0)--(1)
               \
               (2)
        Simulate multiple split for having more forks on different blocks
        '''
        blocks = []

        blocks.append(self.nodes[0].getblockhash(0))
        mark_logs(cc('e', "Genesis block is:         ") + blocks[0], self.nodes, DEBUG_MODE, color='n')

        blocks.extend(self.nodes[1].generate(1)) # block height 1
        mark_logs(cc('c', "Node 1 generated a block: ") + blocks[len(blocks)-1], self.nodes, DEBUG_MODE, color='n')
        self.sync_all()

        print_ordered_tips(self.nodes)
#    Node(0): [0]->[1]
#      |
#      |
#    Node(1): [0]->[1]
#      /\
#     /  \
#    +   Node(2): [0]->[1]
#    |    
# Node(3): [0]->[1]

        mark_logs("Split nodes (1)----x   x---(3)", self.nodes, DEBUG_MODE, color='b')
        self.split_network_2()
        mark_logs("The network is split", self.nodes, DEBUG_MODE, color='e')

        blocks.extend(self.nodes[1].generate(1)) # block height 2
        mark_logs(cc('c', "Node 1 generated a block: ") + blocks[2], self.nodes, DEBUG_MODE, color='n')
        bl2 = blocks[2]
        sync_blocks(self.nodes[:3])

        print_ordered_tips(self.nodes)
#    Node(0): [0]->[1]->[2h]
#      |
#      |
#    Node(1): [0]->[1]->[2h]
#       \
#        \
#        Node(2): [0]->[1]->[2h]
#      
# Node(3): [0]->[1]

        mark_logs("Split nodes (1)----x   x---(2)", self.nodes, DEBUG_MODE, color='b')
        self.split_network(1)
        mark_logs("The network is split", self.nodes, DEBUG_MODE, color='e')

        blocks.extend(self.nodes[1].generate(1)) # block height 3
        mark_logs(cc('c', "Node 1 generated 1 honest block: ") + blocks[3], self.nodes, DEBUG_MODE, color='n')
        bl3 = blocks[3]
        sync_blocks(self.nodes[:2])

        blocks.extend(self.nodes[3].generate(1)) # block height 2M
        mark_logs(cc('c', "Node 3 generated 1 mal block: ") + blocks[4], self.nodes, DEBUG_MODE, color='n')
        sync_blocks([self.nodes[3]])

        blocks.extend(self.nodes[2].generate(1)) # block height 3m
        mark_logs(cc('c', "Node 2 generated 1 mal block: ") + blocks[5], self.nodes, DEBUG_MODE, color='n')
        sync_blocks([self.nodes[2]])

        print_ordered_tips(self.nodes)
#      Node(0): [0]->[1]->[2h]->[3h]
#        |
#      Node(1): [0]->[1]->[2h]->[3h]
#
#          Node(2): [0]->[1]->[2h]->[3m]
#
# Node(3): [0]->[1]->[2M]

        mark_logs("Join network (1)--------(2)", self.nodes, DEBUG_MODE, color='b')
        self.join_network(1)
        mark_logs("The network has joined", self.nodes, DEBUG_MODE, color='e')

        print_ordered_tips(self.nodes)
# Node(0): [0]->[1]->[2h]->[3h]      **Active**
#   |
# Node(1): [0]->[1]->[2h]->[3h]      **Active**
#    \                 \
#     \                 +->[3m]
#      \
#     Node(2): [0]->[1]->[2h]->[3m]  **Active**
#                          \ 
#                           +->[3h] 
# Node(3): [0]->[1]->[2M]

        mark_logs(f"Checking finality of block {bl2}", self.nodes, DEBUG_MODE, color='g')
        get_block_finality(self.nodes, 0, bl2, 3)
        get_block_finality(self.nodes, 1, bl2, 3)
        mark_logs(f"Checking finality of block {bl3}", self.nodes, DEBUG_MODE, color='g')
        get_block_finality(self.nodes, 0, bl3, 2)
        get_block_finality(self.nodes, 1, bl3, 1)

        mark_logs("Join network (1)--------(3)", self.nodes, DEBUG_MODE, color='b')
        self.join_network_2()
        mark_logs("The network has joined", self.nodes, DEBUG_MODE, color='e')

        print_ordered_tips(self.nodes)
#     Node(0): [0]->[1]->[2h]->[3h]      **Active**
#       |                  \
#       |                   +->[3m]
#       |
#       |              +->[2M]
#       |             /
#     Node(1): [0]->[1]->[2h]->[3h]      **Active**
#       /\                  \
#      /  \                  +->[3m]
#     /    \
#    +    Node(2): [0]->[1]->[2h]->[3m]  **Active**
#    |                         \ 
#    |                          +->[3h] 
#    |
# Node(3): [0]->[1]->[2M]
#                 \
#                  +->[2h]->[3h]     **Active**

        blocks.extend(self.nodes[2].generate(1)) # block height 4m
        mark_logs(cc('c', "Node 2 generated 1 mal block: ") + blocks[len(blocks)-1], self.nodes, DEBUG_MODE, color='n')
        sync_blocks(self.nodes)

        try:
            mark_logs(f"Checking finality of block {bl2}", self.nodes, DEBUG_MODE, color='g')
            get_block_finality(self.nodes, 0, bl2, 4)
            get_block_finality(self.nodes, 1, bl2, 4)
            mark_logs(f"Checking finality of block {bl3}", self.nodes, DEBUG_MODE, color='g')
            get_block_finality(self.nodes, 0, bl3, -1)  # Actual value does not matter, this should fail here
            get_block_finality(self.nodes, 1, bl3, -1)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE, color='y')
            mark_logs(f"===> Malicious attach succeeded!", self.nodes, DEBUG_MODE, color='y')

        mark_logs("Syncing network after malicious attack", self.nodes, DEBUG_MODE, color='e')
        sync_blocks(self.nodes, 5, True)

        print_ordered_tips(self.nodes)
#     Node(0): [0]->[1]->[2h]->[3h]
#       |                  \
#       |                   +->[3m]->[4m]       **Active**
#       |
#       |              +->[2M]
#       |             /
#     Node(1): [0]->[1]->[2h]->[3h]  
#       /\                  \
#      /  \                  +->[3m]->[4m]      **Active**
#     /    \
#    +    Node(2): [0]->[1]->[2h]->[3m]->[4m]   **Active**
#    |                         \ 
#    |                          +->[3h]
#    |
# Node(3): [0]->[1]->[2M]
#                 \
#                  +->[2h]->[3h] 
#                       \
#                        +->[3m]->[4m]          **Active**

        mark_logs("Check that all the nodes have the same active tip", self.nodes, DEBUG_MODE, color='g')
        node0ActiveTip = [tip for tip in self.nodes[0].getchaintips() if tip['status'] == 'active'][0]
        node1ActiveTip = [tip for tip in self.nodes[1].getchaintips() if tip['status'] == 'active'][0]
        node2ActiveTip = [tip for tip in self.nodes[2].getchaintips() if tip['status'] == 'active'][0]
        node3ActiveTip = [tip for tip in self.nodes[3].getchaintips() if tip['status'] == 'active'][0]
        assert_equal(node0ActiveTip['hash'], blocks[len(blocks)-1])
        assert_equal(node1ActiveTip['hash'], blocks[len(blocks)-1])
        assert_equal(node2ActiveTip['hash'], blocks[len(blocks)-1])
        assert_equal(node3ActiveTip['hash'], blocks[len(blocks)-1])

if __name__ == '__main__':
    headers().main()
