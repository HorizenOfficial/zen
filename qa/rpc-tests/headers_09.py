#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Sicash developers
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

        if not split:
            # 1 and 2 are joint only if split==false
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:3])
            sync_mempools(self.nodes[1:3])

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def disconnect_nodes(self, from_connection, node_num):
        ip_port = "127.0.0.1:"+str(p2p_port(node_num))
        from_connection.disconnectnode(ip_port)
        # poll until version handshake complete to avoid race conditions
        # with transaction relaying
        while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
            time.sleep(0.1)

    def split_network(self):
        # Split the network of three nodes into nodes 0-1 and 2.
        assert not self.is_network_split
        self.disconnect_nodes(self.nodes[1], 2)
        self.disconnect_nodes(self.nodes[2], 1)
        self.is_network_split = True


    def join_network(self):
        #Join the (previously split) network pieces together: 0-1-2
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 1)
        self.sync_all()
        self.is_network_split = False

    def mark_logs(self, msg):
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)
        self.nodes[2].dbg_log(msg)

    def dump_ordered_tips(self, tip_list):
        sorted_x = sorted(tip_list, key=lambda k: k['status'])
        c = 0
        for y in sorted_x:
            if (c == 0):
                print y 
            else:
                print " ",y 
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
        print blocks[1]
        self.sync_all()

# Node(0): [0]->[1]
#   |
# Node(1): [0]->[1]
#   |
# Node(2): [0]->[1]

        print("\n\nSplit network")
        self.split_network()
        print("The network is split")
        self.mark_logs("The network is split")

        print("\nNode1 generating 30 honest block")
        blocks.extend(self.nodes[1].generate(30)) # block height 2
        print blocks[2]
        self.sync_all()

        print("\nNode2 generating 200 mal block")
        blocks.extend(self.nodes[2].generate(200)) # block height 2
        print blocks[3]
        self.sync_all()

        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print "---"

# Node(0): [0]->[1]->[2h]
#   |                   
# Node(1): [0]->[1]->[2h]
#                       
# Node(2): [0]->[1]->[2m]

#        raw_input("press enter to go on..")

        print("\n\nJoin network")
#        raw_input("press enter to join the netorks..")
        self.mark_logs("Joining network")
        self.join_network()

        time.sleep(10)

        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print "---"

        print("\nNetwork joined") 
        self.mark_logs("Network joined")

        try:
            print "\nChecking finality of block (%d) [%s]" % (0, blocks[2])
            print "  Node0 has: %d" % self.nodes[0].getblockfinalityindex(blocks[2])
            print
        except JSONRPCException,e:
            errorString = e.error['message']
            print errorString

        print("\nNode2 generating 264 mal block")
        blocks.extend(self.nodes[2].generate(264)) # block height 2
#        print blocks[3]
        sync_blocks(self.nodes, 1, True, 8)
#        self.sync_all()
#        blocks.extend(self.nodes[2].generate(1)) # block height 2
#        sync_blocks(self.nodes, 1, True, 3)

# Node(0): [0]->[1]->[2h]
        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print "---"

        print("\nNode2 generating 1 mal block")
        blocks.extend(self.nodes[2].generate(1)) # block height 2
#        print blocks[3]
        sync_blocks(self.nodes, 1, True, 5)
# Node(0): [0]->[1]->[2h]  **Active**
#   |             \     
#   |              +->[2m]    
#   |                   
# Node(1): [0]->[1]->[2h]  **Active**
#   |             \     
#   |              +->[2m]    
#   |                   
#   |                   
# Node(2): [0]->[1]->[2m]  **Active**
#                 \     
#                  +->[2h]    

        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print "---"
#        raw_input("press enter to go on..")

if __name__ == '__main__':
    headers().main()
