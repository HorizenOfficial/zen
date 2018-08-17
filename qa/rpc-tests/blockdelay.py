#!/usr/bin/env python2
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
class blockdelay(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 12)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        # -exportdir option means we must provide a valid path to the destination folder for wallet backups
        ed0 = "-exportdir=" + self.options.tmpdir + "/node0"
        ed1 = "-exportdir=" + self.options.tmpdir + "/node1"
        ed2 = "-exportdir=" + self.options.tmpdir + "/node2"
        ed3 = "-exportdir=" + self.options.tmpdir + "/node3"
        ed4 = "-exportdir=" + self.options.tmpdir + "/node4"
        ed5 = "-exportdir=" + self.options.tmpdir + "/node5"
        ed6 = "-exportdir=" + self.options.tmpdir + "/node6"
        ed7 = "-exportdir=" + self.options.tmpdir + "/node7"
        ed8 = "-exportdir=" + self.options.tmpdir + "/node8"
        ed9 = "-exportdir=" + self.options.tmpdir + "/node9"
        ed10 = "-exportdir=" + self.options.tmpdir + "/node10"
        ed11 = "-exportdir=" + self.options.tmpdir + "/node11"
        extra_args = [["-debug","-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed0],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed1],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed2],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed3],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed4],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed5],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed6],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed7],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed8],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed9],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed10],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed11]]
        self.nodes = start_nodes(12, self.options.tmpdir, extra_args)

        if not split:
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:3])
            sync_mempools(self.nodes[1:3])

            connect_nodes_bi(self.nodes, 5, 6)
            sync_blocks(self.nodes[5:7])
            sync_mempools(self.nodes[5:7])

            connect_nodes_bi(self.nodes, 9, 10)
            sync_blocks(self.nodes[9:11])
            sync_mempools(self.nodes[9:11])

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 2, 3)

        connect_nodes_bi(self.nodes, 4, 5)
        connect_nodes_bi(self.nodes, 6, 7)

        connect_nodes_bi(self.nodes, 8, 9)
        connect_nodes_bi(self.nodes, 10, 11)

        self.sync_all(self.nodes[:4],True)
        self.sync_all(self.nodes[4:8],True)
        self.sync_all(self.nodes[8:],True)

    def disconnect_nodes(self, from_connection, node_num):
        ip_port = "127.0.0.1:"+str(p2p_port(node_num))
        from_connection.disconnectnode(ip_port)
        # poll until version handshake complete to avoid race conditions
        # with transaction relaying
        while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
            time.sleep(0.1)

    def split_network(self,nodes,x,y):
        # Split the network of four nodes into nodes 0/1 and 2/3.
        print nodes
        self.disconnect_nodes(nodes[1], y)
        self.disconnect_nodes(nodes[2], x)

    def join_network(self,nodes):
         #Join the (previously split) network halves together.
        connect_nodes_bi(nodes, 1, 2)
        connect_nodes_bi(nodes, 0, 3)
        # sync_blocks(self.nodes[1:3],1,True)
        sync_mempools(nodes[1:3])
    def sync_all(self,nodes,split):
        if split:
            sync_blocks(nodes[:2])
            sync_blocks(nodes[2:])
            sync_mempools(nodes[:2])
            sync_mempools(nodes[2:])
        else:
            sync_blocks(nodes)
            sync_mempools(nodes)

    def malicious_mining(self, winner,nodes):
        blocks = []
        nodes = []
        if   (winner == "mainchain"):
            nodes = self.nodes[:4]  
        elif (winner == "attacker"):
            nodes = self.nodes[4:8]
        elif (winner == "tie"): 
            nodes = self.nodes[8:]
        
        blocks.append(nodes[0].getblockhash(0))
        print("\n\nGenesis block is: " + blocks[0])

        print("\n\nGenerating initial blockchain")
        blocks.extend(nodes[0].generate(1)) # block height 1
        self.sync_all(nodes,False)
        blocks.extend(nodes[1].generate(1)) # block height 2
        self.sync_all(nodes,False)
        blocks.extend(nodes[2].generate(1)) # block height 3
        self.sync_all(nodes,False)
        blocks.extend(nodes[3].generate(1)) # block height 4
        self.sync_all(nodes,False)
        
        if   (winner == "mainchain"):
            print("\n\nSplit network")
            self.split_network(nodes,1,2)
        elif (winner == "attacker"):
            print("\n\nSplit network")
            self.split_network(nodes,5,6)
        elif (winner == "tie"): 
            print("\n\nSplit network")
            self.split_network(nodes,9,10)



        # Main chain
        print("\n\nGenerating 2 parallel chains with different length")
        blocks.extend(nodes[0].generate(6)) # block height 5 -6 -7 -8 - 9 - 10
        self.sync_all(nodes,True)
        blocks.extend(nodes[1].generate(6)) # block height 11-12-13-14-15-16
        self.sync_all(nodes,True)

        # Malicious nodes mining privately faster
        nodes[2].generate(10) # block height 5 - 6 -7 -8 -9-10 -11 12 13 14
        self.sync_all(nodes,True)
        nodes[3].generate(3) # block height 15 - 16 - 17
        self.sync_all(nodes,True)

        nodes[3].generate(3) # block height 18 - 19 - 20
        self.sync_all(nodes,True)
        if   (winner == "mainchain"):
            nodes[3].generate(61) # block height 21-82  
        elif (winner == "attacker"):
            nodes[3].generate(163)
        elif (winner == "tie"): # in case of a tie
            nodes[3].generate(62)
        self.sync_all(nodes,True)

        print("\n\nJoin network")
        raw_input("join????")
        self.join_network(nodes)
        time.sleep(2)
        print("\n\nNetwork joined")
        # self.nodes[3].generate(61)
        time.sleep(5)
        # sync_blocks(self.nodes,1,True)
        sync_mempools(nodes)
        print nodes[0].getblockcount() , len(blocks)-1
        if   (winner == "mainchain"):
            assert nodes[0].getblockcount() <= len(blocks)-1
        elif (winner == "attacker") or (winner == "tie"):
            assert nodes[0].getblockcount() > len(blocks)-1

    def run_test(self):
       self.malicious_mining("mainchain",self.nodes[:3])
       self.malicious_mining("attacker",self.nodes[4:8])
       self.malicious_mining("tie",self.nodes[8:])

if __name__ == '__main__':
    blockdelay().main()