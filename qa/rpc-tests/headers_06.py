#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, start_node, connect_nodes, stop_node, stop_nodes, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, p2p_port, check_json_precision, \
    disconnect_nodes
import traceback
import os,sys
import shutil
from random import randint
from decimal import Decimal
import logging
import operator

import time
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
        disconnect_nodes(self.nodes[1], 3)
        disconnect_nodes(self.nodes[3], 1)

    def join_network_2(self):
        connect_nodes_bi(self.nodes, 1, 3)

    def mark_logs(self, msg):
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)
        self.nodes[2].dbg_log(msg)
        self.nodes[3].dbg_log(msg)

    def dump_ordered_tips(self, tip_list):
        sorted_x = sorted(tip_list, key=lambda k: k['status'])
        c = 0
        for y in sorted_x:
            if (c == 0):
                print(y)
            else:
                print(" ",y)
            c = 1

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
        print("\n\nGenesis block is:\n" + blocks[0])

        s = "Node 1 generates a block"
        print("\n" + s)
        self.mark_logs(s)

        blocks.extend(self.nodes[1].generate(1)) # block height 1
        print(blocks[len(blocks)-1])
        self.sync_all()

#    Node(0): [0]->[1]
#      |
#      |
#    Node(1): [0]->[1]
#      /\
#     /  \
#    +   Node(2): [0]->[1]
#    |    
# Node(3): [0]->[1]

        print("\n\nSplit nodes (1)----x   x---(3)")
        self.split_network_2()
        print("The network is split")
        self.mark_logs("The network is split 2")

        s = "Node 1 generates a block"
        print("\n" + s)
        self.mark_logs(s)

        blocks.extend(self.nodes[1].generate(1)) # block height 2
        bl2 = blocks[2]
        print(bl2)
        time.sleep(2)

#    Node(0): [0]->[1]->[2h]
#      |
#      |
#    Node(1): [0]->[1]->[2h]
#       \
#        \
#        Node(2): [0]->[1]->[2h]
#      
# Node(3): [0]->[1]

        print("\n\nSplit nodes (1)----x   x---(2)")
        self.split_network(1)
        print("The network is split")
        self.mark_logs("The network is split")

        print("\nNode1 generating 1 honest block")
        blocks.extend(self.nodes[1].generate(1)) # block height 3
        bl3 = blocks[3]
        print(bl3)
        time.sleep(2)

        print("\nNode3 generating 1 mal block")
        blocks.extend(self.nodes[3].generate(1)) # block height 2M
        for i in range(4, 5):
            print(blocks[i])
        time.sleep(2)

#        raw_input("press enter to go on..")

        print("\nNode2 generating 1 mal block")
        blocks.extend(self.nodes[2].generate(1)) # block height 2m
        for i in range(5, 6):
            print(blocks[i])
        time.sleep(2)

#      Node(0): [0]->[1]->[2h]->[3h]
#        |
#        |
#      Node(1): [0]->[1]->[2h]->[3h]
#          
#           
#          Node(2): [0]->[1]->[2h]->[3m]
#        
# Node(3): [0]->[1]->[2M]

#        raw_input("press enter to go on..")

        print("\n\nJoin nodes (1)--(2)")
        # raw_input("press enter to join the netorks..")
        self.mark_logs("Joining network")
        self.join_network(1)

        time.sleep(4)
        print("\nNetwork joined") 
        self.mark_logs("Network joined")

        for i in range(0, 4):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print("---")

#      Node(0): [0]->[1]->[2h]->[3h]      **Active**
#        |                  
#        |                  
#        |
#      Node(1): [0]->[1]->[2h]->[3h]      **Active**
#         \                 \
#          \                 +->[3m]
#           \
#          Node(2): [0]->[1]->[2h]->[3m]  **Active**
#                               \ 
#                                +->[3h] 
#        
# Node(3): [0]->[1]->[2M]

#        raw_input("press enter to go on..")

        print("\nChecking finality of block[", bl2, "]")
        print("  Node0 has: %d" % self.nodes[0].getblockfinalityindex(bl2))
        print("  Node1 has: %d" % self.nodes[1].getblockfinalityindex(bl2))
        print("\nChecking finality of block[", bl3, "]")
        print("  Node0 has: %d" % self.nodes[0].getblockfinalityindex(bl3))
        print("  Node1 has: %d" % self.nodes[1].getblockfinalityindex(bl3))

#        raw_input("press enter to go on..")

        print("\n\nJoin nodes (1)--(3)")
        self.mark_logs("Joining network 2")
        self.join_network_2()

        time.sleep(2)
        print("\nNetwork joined") 
        self.mark_logs("Network joined 2")
        time.sleep(2)

        for i in range(0, 4):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print("---")

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

#        raw_input("press enter to go on..")

        print("\nNode2 generating 1 mal block")
        blocks.extend(self.nodes[2].generate(1)) # block height 4m
        n=len(blocks)-1
        print(blocks[n])
        time.sleep(3)

        try:
            print("\nChecking finality of block[", bl2, "]")
            print("  Node0 has: %d" % self.nodes[0].getblockfinalityindex(bl2))
            print("  Node1 has: %d" % self.nodes[1].getblockfinalityindex(bl2))
            print("\nChecking finality of block[", bl3, "]")
            print("  Node0 has: %d" % self.nodes[0].getblockfinalityindex(bl3))
            print("  Node1 has: %d" % self.nodes[1].getblockfinalityindex(bl3))
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString, "\n")

        self.mark_logs("\nSyncing network after malicious attack")
        sync_blocks(self.nodes, 5, True)

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

        for i in range(0, 4):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print("---")

#        raw_input("press enter to go on..")

if __name__ == '__main__':
    headers().main()
