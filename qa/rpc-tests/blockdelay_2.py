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

import time
class blockdelay_2(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(3, self.options.tmpdir)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        #connect_nodes_bi(self.nodes, 0, 2)
        self.is_network_split = split
        self.sync_all()

    def split_network(self):
        # Split the network of three nodes into nodes 0 and 1/2.
#        assert not self.is_network_split
        disconnect_nodes(self.nodes[1], 0)
        disconnect_nodes(self.nodes[0], 1)
        #disconnect_nodes(self.nodes[0], 2)
        #disconnect_nodes(self.nodes[2], 0)
        self.is_network_split = True
        
    def split_network_2(self):
        # Split the network of three nodes into nodes 0, 1 and 2.
        disconnect_nodes(self.nodes[1], 2)
        disconnect_nodes(self.nodes[2], 1)


    def join_network(self):
        #Join the (previously split) network halves together.
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 0)
        
        sync_blocks(self.nodes[1:2])
        sync_mempools(self.nodes[1:2])
        
    def join_network_2(self):
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[2:3])
        sync_mempools(self.nodes[2:3])

    def dump_ordered_tips(self, tip_list):
        sorted_x = sorted(tip_list, key=lambda k: k['status'])
        c = 0
        for y in sorted_x:
            if (c == 0):
                print(y)
            else:
                print(" ", y)
            c = 1

    def mark_logs(self, msg):
        for x in self.nodes:
            x.dbg_log(msg)

    def get_network_info(self):
        for i in range(0, len(self.nodes)):
            n = len(self.nodes[i].getpeerinfo())
            url = self.nodes[i].url
            print("\nNode %s" % i + " (address: %s"% url+" and RPC port: "+str(p2p_port(i))+") has %s peers" % n)
            if (n > 0):
                self.getpeers(i, url)
        print("\n")
                
    def getpeers(self, node_index, url):
        for node in self.nodes[node_index].getpeerinfo():
            for i in range(0, len(self.nodes)):
                ip_port = "127.0.0.1:"+str(p2p_port(i))
                if (node['addr'] == ip_port):
                    print("\tID: %s" % node['id']+" --> Address: %s" % ip_port)
    
    def printchaintips(self):
        for i in range(0, len(self.nodes)):
            print("Node%d  ---" % i)
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print("---")
            
    def run_test(self):
        blocks = []
        self.bl_count = 0
        net_len = len(self.nodes)

        blocks.append(self.nodes[0].getblockhash(0))
        print("\n\nGenesis block is: " + blocks[0])
        # raw_input("press enter to start..")
        try:
            print("\nChecking finality of block (%d) [%s]" % (0, blocks[0]))
            print("  Node0 has: %d" % self.nodes[0].getblockfinalityindex(blocks[0]))
            print
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)

        print("\n\nNetwork topology:")
        print("\n0 <--> 1 ... 0 <--> 2 ... 1 <--> 2")
        self.get_network_info()

        print("\n\nGenerating initial blockchain 3 blocks")
        blocks.extend(self.nodes[0].generate(1)) # block height 1
        print(blocks[len(blocks)-1])
        self.sync_all()
        blocks.extend(self.nodes[1].generate(1)) # block height 2
        print(blocks[len(blocks)-1])
        self.sync_all()
        blocks.extend(self.nodes[2].generate(1)) # block height 3
        print(blocks[len(blocks)-1])
        self.sync_all()
        print("Blocks generated\n")

# Node(0): [0]->..->[3]
#   |
# Node(1): [0]->..->[3]
#   |
# Node(2): [0]->..->[3]

        self.printchaintips()
        print("\nGet best block hash for all nodes:")
        for i in range(0, net_len):
            print("Best block hash for node "+str(i)+": "+self.nodes[i].getbestblockhash())
        
        first_main_blockhash = blocks[len(blocks)-1]
        first_main_blockhash_index = len(blocks)-1
        
        print("Assert best block hash of node 0 == best block hash of node 1")
        assert self.nodes[0].getbestblockhash() == self.nodes[1].getbestblockhash()
        print("Assert best block hash of node 1 == best block hash of node 2")
        assert self.nodes[1].getbestblockhash() == self.nodes[2].getbestblockhash()
        print("Assert best block hash of node 1 == best block hash of node 2")
        assert self.nodes[0].getbestblockhash() == self.nodes[2].getbestblockhash()
        print("Assert best block hash of node 0 == "+first_main_blockhash)
        assert self.nodes[0].getbestblockhash() == first_main_blockhash
        
        finalities = []

        print("\nChecking finality of first honest block [%s]" %  first_main_blockhash)
        for i in range(0, net_len):
            print("  Node%d sees:"  % i)
            try:
                finalities.append(self.nodes[i].getblockfinalityindex(first_main_blockhash)) 
                print("      finality: %d" % finalities[i])
                print
            except JSONRPCException as e:
                errorString = e.error['message']
                print("      " + errorString)
                print

        assert finalities[0] == finalities[1]
        assert finalities[1] == finalities[2]
        assert finalities[0] == finalities[2]
        
        print("\nFirst split. Best block hash is %s: "+first_main_blockhash)
        print("\n\nSplit network")
        self.split_network()
        print("The network is splitted")
        print("\n0 ... 1 <--> 2")
        self.get_network_info()

        bl = []
        
        print("\nGenerating 6 blocks on node 0")
        for i in range (0, 6):
            blocks.extend(self.nodes[0].generate(1))
            bl.append(blocks[len(blocks)-1])
            print(bl[len(bl)-1])

        last_main_blockhash_node_0 = blocks[len(blocks)-1]
        
        print("\nSynch...")
        sync_blocks(self.nodes[0:1])
        sync_mempools(self.nodes[0:1])

        self.printchaintips()
        print("\nGet best block hash for all nodes:")
        for i in range(0, net_len):
            print("Best block hash for node "+str(i)+": "+self.nodes[i].getbestblockhash())
        
        print("\nGenerating 15 blocks on node 1")
        for i in range (0, 15):
            blocks.extend(self.nodes[1].generate(1))
            bl.append(blocks[len(blocks)-1])
            print(bl[len(bl)-1])
            
        print("\nSynch...")
        sync_blocks(self.nodes[1:3])
        sync_mempools(self.nodes[1:3])
        
        last_main_blockhash_node_1 = blocks[len(blocks)-1]
        last_main_blockhash_node_2 = blocks[len(blocks)-1]
        
        self.printchaintips()
        print("\nGet best block hash for all nodes:")
        for i in range(0, net_len):
            print("Best block hash for node "+str(i)+": "+self.nodes[i].getbestblockhash())

# Node(0): [0]->..->[3]
#                    +-->[4]...->[9]
#      
# Node(1): [0]->..->[3]->[4]...->[18]
# |
# Node(1): [0]->..->[3]->[4]...->[18]

        print("Assert best block hash of node 0 == "+last_main_blockhash_node_0)
        assert self.nodes[0].getbestblockhash() == last_main_blockhash_node_0
        print("Assert best block hash of node 0 == "+last_main_blockhash_node_1)
        assert self.nodes[1].getbestblockhash() == last_main_blockhash_node_1
        print("Assert best block hash of node 2 == "+last_main_blockhash_node_2)
        assert self.nodes[2].getbestblockhash() == last_main_blockhash_node_2
        print("Assert best block hash of node 1 == best block hash of node 2")
        assert self.nodes[1].getbestblockhash() == self.nodes[2].getbestblockhash()
        print("Assert best block hash of node 1 != best block hash of node 0")
        assert self.nodes[1].getbestblockhash() != self.nodes[0].getbestblockhash()

        print("\nGenerating 10 blocks on node 1")
        blocks.extend(self.nodes[1].generate(10))

        print("\nSynch...")
        sync_blocks(self.nodes[1:3])
        sync_mempools(self.nodes[1:3])
        
        print("\nGenerating 3 blocks on node 2")
        blocks.extend(self.nodes[2].generate(3))

        print("\nSynch...")
        sync_blocks(self.nodes[1:3])
        sync_mempools(self.nodes[1:3])
        
        last_main_blockhash_node_1 = blocks[len(blocks)-1]
        last_main_blockhash_node_2 = blocks[len(blocks)-1]

        self.printchaintips()
        print("\nGet best block hash for all nodes:")
        for i in range(0, net_len):
            print("Best block hash for node "+str(i)+": "+self.nodes[i].getbestblockhash())

# Node(0): [0]->..->[3]
#                    +-->[4]...->[9]
#      
# Node(1): [0]->..->[3]->[4]...->[18]->[19]...->[31]
# |
# Node(1): [0]->..->[3]->[4]...->[18]->[19]...->[31]

        print("Assert best block hash of node 1 == "+last_main_blockhash_node_0)
        assert self.nodes[0].getbestblockhash() == last_main_blockhash_node_0
        print("Assert best block hash of node 1 == "+last_main_blockhash_node_1)
        assert self.nodes[1].getbestblockhash() == last_main_blockhash_node_1
        print("Assert best block hash of node 1 == "+last_main_blockhash_node_2)
        assert self.nodes[2].getbestblockhash() == last_main_blockhash_node_2
        print("Assert best block hash of node 1 == best block hash of node 2")
        assert self.nodes[1].getbestblockhash() == self.nodes[2].getbestblockhash()
        print("Assert best block hash of node 1 != best block hash of node 0")
        assert self.nodes[1].getbestblockhash() != self.nodes[0].getbestblockhash()
        
        second_main_blockhash = blocks[len(blocks)-1]
        second_main_blockhash_index = len(blocks)-1
        
        print("\nSecond split. Best block hash is %s: "+second_main_blockhash)
        
        print("\n\nSplit network again")
        self.split_network_2()
        print("The network is split again")
        print("\n0 ... 1 ... 2")
        self.get_network_info()
        
        print("\nGenerating 8 blocks on node 0")
        blocks.extend(self.nodes[0].generate(8))

        print("\nSynch...")
        sync_blocks(self.nodes[0:1])
        sync_mempools(self.nodes[0:1])
        
        last_main_blockhash_node_0 = blocks[len(blocks)-1]

        self.printchaintips()
        print("\nGet best block hash for all nodes:")
        for i in range(0, net_len):
            print("Best block hash for node "+str(i)+": "+self.nodes[i].getbestblockhash())
        
        print("\nGenerating 8 blocks on node 1")
        blocks.extend(self.nodes[1].generate(8))
        
        print("\nSynch...")
        sync_blocks(self.nodes[1:2])
        sync_mempools(self.nodes[1:2])

        last_main_blockhash_node_1 = blocks[len(blocks)-1]
        last_main_blockhash_node_1_index = len(blocks)-1
        
        self.printchaintips()
        print("\nGet best block hash for all nodes:")
        for i in range(0, net_len):
            print("Best block hash for node "+str(i)+": "+self.nodes[i].getbestblockhash())
            
        print("\nGenerating 6 blocks on node 2")
        blocks.extend(self.nodes[2].generate(6))

        print("\nSynch...")
        sync_blocks(self.nodes[2:3])
        sync_mempools(self.nodes[2:3])

        last_main_blockhash_node_2 = blocks[len(blocks)-1]
        
        self.printchaintips()
        print("\nGet best block hash for all nodes:")
        for i in range(0, net_len):
            print("Best block hash for node "+str(i)+": "+self.nodes[i].getbestblockhash())

        print("Assert best block hash of node 0 == "+last_main_blockhash_node_0)
        assert self.nodes[0].getbestblockhash() == last_main_blockhash_node_0
        print("Assert best block hash of node 1 == "+last_main_blockhash_node_1)
        assert self.nodes[1].getbestblockhash() == last_main_blockhash_node_1
        print("Assert best block hash of node 2 == "+last_main_blockhash_node_2)
        assert self.nodes[2].getbestblockhash() == last_main_blockhash_node_2
        print("Assert best block hash of node 1 != best block hash of node 2")
        assert self.nodes[1].getbestblockhash() != self.nodes[2].getbestblockhash()
        print("Assert best block hash of node 1 != best block hash of node 0")
        assert self.nodes[1].getbestblockhash() != self.nodes[0].getbestblockhash()
        print("Assert best block hash of node 0 != best block hash of node 2")
        assert self.nodes[0].getbestblockhash() != self.nodes[2].getbestblockhash()

# Node(0): [0]->..->[3]
#                    +-->[4]...->[9]->[10]...->[17]
#      
# Node(1): [0]->..->[3]->[4]...->[18]->[19]...->[31]
#                                                 +-->[32]...->[39]
# 
# Node(1): [0]->..->[3]->[4]...->[18]->[19]...->[31]
#                                                 +-->[32]...->[37]

        print("\nJoining network...")
        self.join_network()
        time.sleep(5)
        print("\nNetwork joined")
        self.get_network_info()

        self.printchaintips()
        print("\nGet best block hash for all nodes:")
        for i in range(0, net_len):
            print("Best block hash for node "+str(i)+": "+self.nodes[i].getbestblockhash())
            
            
        print("Assert best block hash of node 0 == "+last_main_blockhash_node_0)
        assert self.nodes[0].getbestblockhash() == last_main_blockhash_node_0
        print("Assert best block hash of node 1 == "+last_main_blockhash_node_1)
        assert self.nodes[1].getbestblockhash() == last_main_blockhash_node_1
        print("Assert best block hash of node 2 == "+last_main_blockhash_node_2)
        assert self.nodes[2].getbestblockhash() == last_main_blockhash_node_2
        print("Assert best block hash of node 1 != best block hash of node 2")
        assert self.nodes[1].getbestblockhash() != self.nodes[2].getbestblockhash()
        print("Assert best block hash of node 1 != best block hash of node 0")
        assert self.nodes[1].getbestblockhash() != self.nodes[0].getbestblockhash()
        print("Assert best block hash of node 0 != best block hash of node 2")
        assert self.nodes[0].getbestblockhash() != self.nodes[2].getbestblockhash()
        
        print("\nChecking finality of first honest block [%s]" %  blocks[first_main_blockhash_index+1])
        for i in range(0, net_len):
            print("  Node%d sees:"  % i)
            try:
                finalities[i]=self.nodes[i].getblockfinalityindex(blocks[first_main_blockhash_index+1])
                print("      finality: %d" % finalities[i])
                print
            except JSONRPCException as e:
                errorString = e.error['message']
                print("      " + errorString)
                print
         
        print("\nFinality for node 0 must be 69")
        assert finalities[0] == 69
        
        print("\nGenerating "+str(finalities[0]-1)+" blocks on node 1")
        self.nodes[1].generate(finalities[0]-1)
        sync_blocks(self.nodes[1:2])
        sync_mempools(self.nodes[1:2])
        time.sleep(2)
        self.printchaintips()
        
        
        print("Assert best block hash of node 1 != best block hash of node 2")
        assert self.nodes[1].getbestblockhash() != self.nodes[2].getbestblockhash()
        print("Assert best block hash of node 1 != best block hash of node 0")
        assert self.nodes[1].getbestblockhash() != self.nodes[0].getbestblockhash()
        print("Assert best block hash of node 0 != best block hash of node 2")
        assert self.nodes[0].getbestblockhash() != self.nodes[2].getbestblockhash()
        
        
        print("\nGenerating 1 blocks on node 1")
        self.nodes[1].generate(1)
        sync_blocks(self.nodes[1:2])
        sync_mempools(self.nodes[1:2])
        time.sleep(2)
        self.printchaintips()
        
        print("Assert best block hash of node 1 != best block hash of node 2")
        assert self.nodes[1].getbestblockhash() != self.nodes[2].getbestblockhash()
        print("Assert best block hash of node 1 == best block hash of node 0")
        assert self.nodes[1].getbestblockhash() == self.nodes[0].getbestblockhash()
        print("Assert best block hash of node 0 != best block hash of node 2")
        assert self.nodes[0].getbestblockhash() != self.nodes[2].getbestblockhash()
        
#         while (self.nodes[0].getbestblockhash() != self.nodes[1].getbestblockhash()):
#             n_1 = self.nodes[0].getblockchaininfo()['blocks']
#             n_2 = self.nodes[1].getblockchaininfo()['blocks']
#             if (n_1<n_2):
#                 blocks.extend(self.nodes[0].generate(n_2-n_1))
#                 last_main_blockhash_node_0 = blocks[len(blocks)-1]
#                 print("\nGenerated "+str(n_2-n_1)+" blocks on node 0: "+blocks[len(blocks)-1])
#                 print("Synch...")
#                 sync_blocks(self.nodes[0:1])
#                 sync_mempools(self.nodes[0:1])
#             if (n_1>n_2):
#                 blocks.extend(self.nodes[1].generate(n_1-n_2))
#                 last_main_blockhash_node_1 = blocks[len(blocks)-1]
#                 print("\nGenerated "+str(n_1-n_2)+" blocks on node 1: "+blocks[len(blocks)-1])
#                 print("Synch...")
#                 sync_blocks(self.nodes[1:2])
#                 sync_mempools(self.nodes[1:2])
#             if (n_1==n_2):
#                 blocks.extend(self.nodes[0].generate(1))
#                 last_main_blockhash_node_0 = blocks[len(blocks)-1]
#                 print("\nGenerated 1 blocks on node 0: "+blocks[len(blocks)-1])
#                 print("Synch...")
#                 sync_blocks(self.nodes[0:1])
#                 sync_mempools(self.nodes[0:1])
#             
#             time.sleep(1)
            

        
        print("\nGet best block hash for all nodes:")
        for i in range(0, net_len):
            print("Best block hash for node "+str(i)+": "+self.nodes[i].getbestblockhash())
            
        print("\nJoining network again...")
        self.join_network_2()
        time.sleep(5)
        print("\nNetwork joined")
        self.get_network_info()

        self.printchaintips()
        print("\nGet best block hash for all nodes:")
        for i in range(0, net_len):
            print("Best block hash for node "+str(i)+": "+self.nodes[i].getbestblockhash())
            
        
# +---Node(0): [0]->..->[3]->[4]...->[18]->[19]...->[31]->[32]...->[108]
# |                   
# |     
# +---Node(1): [0]->..->[3]->[4]...->[18]->[19]...->[31]->[32]...->[108]
# |
# |
# +---Node(1): [0]->..->[3]->[4]...->[18]->[19]...->[31]->[32]...->[108]

        print("Assert best block hash of node 1 == best block hash of node 2")
        assert self.nodes[1].getbestblockhash() == self.nodes[2].getbestblockhash()
        print("Assert best block hash of node 1 == best block hash of node 0")
        assert self.nodes[1].getbestblockhash() == self.nodes[0].getbestblockhash()
        print("Assert best block hash of node 0 == best block hash of node 2")
        assert self.nodes[0].getbestblockhash() == self.nodes[2].getbestblockhash()
        
#         print "\nChecking finality of second honest block [%s]" %  second_main_blockhash
#         for i in range(0, net_len):
#             try:
#                 finalities[i]=self.nodes[i].getblockfinalityindex(second_main_blockhash)
#                 print "  Node%d sees:"  % i
#                 print "      finality: %d" % finalities[i]
#                 print
#             except JSONRPCException as e:
#                 errorString = e.error['message']
#                 print "      " + errorString
#                 print
         
#         while self.nodes[0].getbestblockhash() != self.nodes[2].getbestblockhash():
#             n_1 = self.nodes[0].getblockchaininfo()['blocks']
#             n_2 = self.nodes[2].getblockchaininfo()['blocks']
#             if (n_1<n_2):
#                 blocks.extend(self.nodes[0].generate(n_2-n_1))
#                 last_main_blockhash_node_0 = blocks[len(blocks)-1]
#                 print("\nGenerated "+str(n_2-n_1)+" blocks on node 0: "+blocks[len(blocks)-1])
#                 print("Synch...")
#                 self.sync_all()
#             if (n_1>n_2):
#                 blocks.extend(self.nodes[2].generate(n_1-n_2))
#                 last_main_blockhash_node_2 = blocks[len(blocks)-1]
#                 print("\nGenerated "+str(n_1-n_2)+" blocks on node 2: "+blocks[len(blocks)-1])
#                 print("Synch...")
#                 self.sync_all()
#             
#             time.sleep(1)
            

#         print "\nGenerating "+str(finalities[2])+" blocks on node 2"
#         self.nodes[2].generate(finalities[2])
#         self.sync_all()
#         time.sleep(1)
#         self.printchaintips()
#         
#         assert self.nodes[1].getbestblockhash() == self.nodes[2].getbestblockhash()
#         assert self.nodes[1].getbestblockhash() == self.nodes[0].getbestblockhash()
#         assert self.nodes[0].getbestblockhash() == self.nodes[2].getbestblockhash()
#         
#         
#         print "\nChecking finality of second honest block [%s]" %  blocks[len(blocks)-1]
#         for i in range(0, net_len):
#             try:
#                 finalities[i]=self.nodes[i].getblockfinalityindex(blocks[len(blocks)-1])
#                 print "  Node%d sees:"  % i
#                 print "      finality: %d" % finalities[i]
#                 print
#             except JSONRPCException as e:
#                 errorString = e.error['message']
#                 print "      " + errorString
#                 print
        
        print("\nThird split. Best block hash is %s: "+self.nodes[1].getbestblockhash())
        
        print("\n\nSplit network again")
        self.split_network_2()
        print("The network is split again")
        print("\n0 <--> 1 ... 2")
        self.get_network_info()
        
        print("\nGenerating 10 blocks on node 1")
        blocks.extend(self.nodes[1].generate(10))

        print("\nSynch...")
        sync_blocks(self.nodes[0:2])
        sync_mempools(self.nodes[0:2])
        
        last_main_blockhash_node_1 = blocks[len(blocks)-1]
        last_main_blockhash_node_1_index = len(blocks)-1

        self.printchaintips()
        print("\nGet best block hash for all nodes:")
        for i in range(0, net_len):
            print("Best block hash for node "+str(i)+": "+self.nodes[i].getbestblockhash())
        
        print("\nGenerating 8 blocks on node 2")
        blocks.extend(self.nodes[2].generate(8))

        print("\nSynch...")
        sync_blocks(self.nodes[2:3])
        sync_mempools(self.nodes[2:3])

        last_main_blockhash_node_2 = blocks[len(blocks)-1]
        last_main_blockhash_node_2_index = len(blocks)-1
        
        self.printchaintips()
        print("\nGet best block hash for all nodes:")
        for i in range(0, net_len):
            print("Best block hash for node "+str(i)+": "+self.nodes[i].getbestblockhash())

# +---Node(0): [0]->..->[3]->[4]...->[18]->[19]...->[31]->[32]...->[108]->[109]...->[118]
# |                   
# |     
# +---Node(1): [0]->..->[3]->[4]...->[18]->[19]...->[31]->[32]...->[108]->[109]...->[118]
# 
# 
# Node(1): [0]->..->[3]->[4]...->[18]->[19]...->[31]->[32]...->[108]->[109]...->[116]

        print("Assert best block hash of node 0 == "+last_main_blockhash_node_1)
        assert self.nodes[0].getbestblockhash() == last_main_blockhash_node_1
        print("Assert best block hash of node 1 == "+last_main_blockhash_node_1)
        assert self.nodes[1].getbestblockhash() == last_main_blockhash_node_1
        print("Assert best block hash of node 2 == "+last_main_blockhash_node_2)
        assert self.nodes[2].getbestblockhash() == last_main_blockhash_node_2
        print("Assert best block hash of node 1 != best block hash of node 2")
        assert self.nodes[1].getbestblockhash() != self.nodes[2].getbestblockhash()
        print("Assert best block hash of node 1 == best block hash of node 0")
        assert self.nodes[1].getbestblockhash() == self.nodes[0].getbestblockhash()
        print("Assert best block hash of node 0 != best block hash of node 2")
        assert self.nodes[0].getbestblockhash() != self.nodes[2].getbestblockhash()
        
        print("\nJoining network...")
        self.join_network_2()
        time.sleep(5)
        print("\nNetwork joined")
        self.get_network_info()

        self.printchaintips()
        print("\nGet best block hash for all nodes:")
        for i in range(0, net_len):
            print("Best block hash for node "+str(i)+": "+self.nodes[i].getbestblockhash())
            

        print("Assert best block hash of node 0 == "+last_main_blockhash_node_1)
        assert self.nodes[0].getbestblockhash() == last_main_blockhash_node_1
        print("Assert best block hash of node 1 == "+last_main_blockhash_node_1)
        assert self.nodes[1].getbestblockhash() == last_main_blockhash_node_1
        print("Assert best block hash of node 2 == "+last_main_blockhash_node_2)
        assert self.nodes[2].getbestblockhash() == last_main_blockhash_node_2
        print("Assert best block hash of node 1 != best block hash of node 2")
        assert self.nodes[1].getbestblockhash() != self.nodes[2].getbestblockhash()
        print("Assert best block hash of node 1 == best block hash of node 0")
        assert self.nodes[1].getbestblockhash() == self.nodes[0].getbestblockhash()
        print("Assert best block hash of node 0 != best block hash of node 2")
        assert self.nodes[0].getbestblockhash() != self.nodes[2].getbestblockhash()
        
        print("\nChecking finality of first honest block [%s]" %  blocks[last_main_blockhash_node_1_index+1])
        for i in range(0, net_len):
            print("  Node%d sees:"  % i)
            try:
                finalities[i]=self.nodes[i].getblockfinalityindex(blocks[last_main_blockhash_node_1_index+1])
                print("      finality: %d" % finalities[i])
                print
            except JSONRPCException as e:
                errorString = e.error['message']
                print("      " + errorString)
                print
         
        print("\nFinality for node 2 must be 26")
        assert finalities[2] == 26
        
        print("\nGenerating "+str(finalities[2]-1)+" blocks on node 1")
        self.nodes[1].generate(finalities[2]-1)
        sync_blocks(self.nodes[2:3])
        sync_mempools(self.nodes[2:3])
        time.sleep(2)
        self.printchaintips()
        
        print("Assert best block hash of node 1 != best block hash of node 2")
        assert self.nodes[1].getbestblockhash() != self.nodes[2].getbestblockhash()
        print("Assert best block hash of node 1 == best block hash of node 0")
        assert self.nodes[1].getbestblockhash() == self.nodes[0].getbestblockhash()
        print("Assert best block hash of node 0 != best block hash of node 2")
        assert self.nodes[0].getbestblockhash() != self.nodes[2].getbestblockhash()
        
        
        print("\nGenerating 1 blocks on node 1")
        self.nodes[1].generate(1)
        sync_blocks(self.nodes[2:3])
        sync_mempools(self.nodes[2:3])
        time.sleep(2)
        self.printchaintips()
        
        print("Assert best block hash of node 1 == best block hash of node 2")
        assert self.nodes[1].getbestblockhash() == self.nodes[2].getbestblockhash()
        print("Assert best block hash of node 1 == best block hash of node 0")
        assert self.nodes[1].getbestblockhash() == self.nodes[0].getbestblockhash()
        print("Assert best block hash of node 0 == best block hash of node 2")
        assert self.nodes[0].getbestblockhash() == self.nodes[2].getbestblockhash()
        
        
if __name__ == '__main__':
    blockdelay_2().main()
