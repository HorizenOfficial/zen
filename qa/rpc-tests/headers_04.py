#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, start_node, connect_nodes, stop_node, stop_nodes, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, p2p_port, check_json_precision
import traceback
import os,sys
import shutil
from random import randint
from decimal import Decimal
import logging

import time
class headers(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_nodes(self):
        self.nodes = start_nodes(3, self.options.tmpdir)

    def mark_logs(self, msg):
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)
        self.nodes[2].dbg_log(msg)

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
        blocks = []
        self.bl_count = 0

        blocks.append(self.nodes[0].getblockhash(0))
        print("\n\nGenesis block is:\n" + blocks[0])

        s = "Node 1 generates a block"
        print("\n\n" + s + "\n")
        self.mark_logs(s)

        blocks.extend(self.nodes[1].generate(1)) # block height 1
        print(blocks[1])
        self.sync_all()

# Node(0): [0]->[1]
#   |
# Node(1): [0]->[1]
#   |
# Node(2): [0]->[1]

        print("\n\nSplit network")
        self.split_network(1)
        print("The network is split")
        self.mark_logs("The network is split")

        print("\nNode1 generating 6 honest block")
        blocks.extend(self.nodes[1].generate(6)) # block height 2--7
        bl2 = blocks[2]
        for i in range(2, 8):
            print(blocks[i])
        self.sync_all()

        print("\nNode2 generating 6 mal block")
        blocks.extend(self.nodes[2].generate(6)) # block height 2--7
        for i in range(8, 14):
            print(blocks[i])
        self.sync_all()

        print
        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print("---")

# Node(0): [0]->[1]->..->[7h]
#   |                   
# Node(1): [0]->[1]->..->[7h]
#                       
# Node(2): [0]->[1]->..->[7m]

#        raw_input("press enter to go on..")
        try:
            print("\nChecking finality of block[", bl2, "]")
            print("  Node0 has: %d" % self.nodes[0].getblockfinalityindex(bl2))
            print("  Node1 has: %d" % self.nodes[1].getblockfinalityindex(bl2))
            print
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)

        print("\n\nJoin network")
#        raw_input("press enter to join the netorks..")
        self.mark_logs("Joining network")
        self.join_network()

        print("\nNetwork joined\n") 
        self.mark_logs("Network joined")

        print
        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print("---")

#        raw_input("press enter to go on..")

# Node(0): [0]->[1]->..->[7h]  **Active**    
#   |             
#   |                 
#   |                   
# Node(1): [0]->[1]->..->[7h]  **Active**    
#   |             \     
#   |              +->..->[7m]    
#   |                   
#   |                   
# Node(2): [0]->[1]->..->[7m]  **Active**    
#                 \     
#                  +->..->[7h]    

        try:
            print("\nChecking finality of block[", bl2, "]")
            print("  Node0 has: %d" % self.nodes[0].getblockfinalityindex(bl2))
            print("  Node1 has: %d" % self.nodes[1].getblockfinalityindex(bl2))
            print
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)

        print
        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print("---")

        print("\nNode2 generating 1 mal block")
        blocks.extend(self.nodes[2].generate(1)) # block height 8
        print(blocks[len(blocks)-1])
        print
        sync_blocks(self.nodes, 5, True)

# Node(0): [0]->[1]->..->[7h]
#   |             \     
#   |              +->..->[7m]->[8m]   **Active**       
#   |                   
# Node(1): [0]->[1]->..->[7h] 
#   |             \     
#   |              +->..->[7m]->[8m]   **Active**      
#   |                   
#   |                   
# Node(2): [0]->[1]->..->[7m]->[8m]    **Active**    
#                 \     
#                  +->..->[7h]    

        print
        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print("---")

#        raw_input("press enter to go on..")

if __name__ == '__main__':
    headers().main()
