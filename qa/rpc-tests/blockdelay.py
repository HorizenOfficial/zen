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

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4)
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
        '''
        extra_args = [["-debug","-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed0],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed1],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed2],
            ["-debug", "-keypool=100", "-alertnotify=echo %s >> \"" + self.alert_filename + "\"", ed3]]
        '''

        #self.nodes = start_nodes(4, self.options.tmpdir, extra_args)
        self.nodes = start_nodes(4, self.options.tmpdir)


        if not split:
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:3])
            sync_mempools(self.nodes[1:3])

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 2, 3)
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
        # Split the network of four nodes into nodes 0/1 and 2/3.
        assert not self.is_network_split
        self.disconnect_nodes(self.nodes[1], 2)
        self.disconnect_nodes(self.nodes[2], 1)
        self.is_network_split = True


    def join_network(self):
        #Join the (previously split) network halves together.
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 0, 3)
        connect_nodes_bi(self.nodes, 3, 0)
        connect_nodes_bi(self.nodes, 1, 3)
        connect_nodes_bi(self.nodes, 3, 1)
        #sync_blocks(self.nodes[0:3],1,True)
        #sync_mempools(self.nodes[1:3])
        self.sync_all()
        self.is_network_split = False


    def run_test(self):
        blocks = []

        blocks.append(self.nodes[0].getblockhash(0))
        print("\n\nGenesis block is: " + blocks[0])
        # raw_input("press enter to start..")


        print("\n\nGenerating initial blockchain 4 blocks")
        blocks.extend(self.nodes[0].generate(1)) # block height 1
        self.sync_all()
        blocks.extend(self.nodes[1].generate(1)) # block height 2
        self.sync_all()
        blocks.extend(self.nodes[2].generate(1)) # block height 3
        self.sync_all()
        blocks.extend(self.nodes[3].generate(1)) # block height 4
        self.sync_all()
        print("Blocks generated")

        print("\n\nSplit network")
        self.split_network()
        print("The network is split")


        # Main chain
        print("\n\nGenerating 2 parallel chains with different length")

        print("\nGenerating 12 honest blocks")
        blocks.extend(self.nodes[0].generate(6)) # block height 5 -6 -7 -8 - 9 - 10
        self.sync_all()
        blocks.extend(self.nodes[1].generate(6)) # block height 11-12-13-14-15-16
        last_main_blockhash=blocks[len(blocks)-1]
        self.sync_all()
        print("Honest block generated")

        assert self.nodes[0].getbestblockhash() == last_main_blockhash

        # Malicious nodes mining privately faster
        print("\nGenerating 13 malicious blocks")
        self.nodes[2].generate(10) # block height 5 - 6 -7 -8 -9-10 -11 12 13 14
        self.sync_all()
        self.nodes[3].generate(3) # block height 15 - 16 - 17
        self.sync_all()
        print("Malicious block generated")


        print("\n\nJoin network")
        # raw_input("press enter to join the netorks..")
        self.join_network()
        time.sleep(2)
        print("\nNetwork joined")

        print("\nTesting if the current chain is still the honest chain")
        assert self.nodes[0].getbestblockhash() == last_main_blockhash
        print("Confirmed: malicious chain is under penalty")


        print("\nGenerating 64 malicious blocks")       
        self.nodes[3].generate(64)
        print("Malicious block generated")

        time.sleep(10)

        print("\nTesting if the current chain is still the honest chain")
        assert self.nodes[0].getbestblockhash() == last_main_blockhash
        print("Confirmed: malicious chain is under penalty")

        print("\nGenerating 65 more honest blocks")
        self.nodes[0].generate(65)
        print("Honest block generated")

        print("\nGenerating 1 more malicious block")
        last_malicious_blockhash=self.nodes[3].generate(1)[0]
        print("Malicious block generated")

        print("\nWaiting that all network nodes are synced with same chain length")
        sync_blocks(self.nodes, 1, True)
        print("Network nodes are synced")

        print("\nTesting if all the nodes/chains have the same best tip")
        assert (self.nodes[0].getbestblockhash() == self.nodes[1].getbestblockhash()
                == self.nodes[2].getbestblockhash() == self.nodes[3].getbestblockhash())
        print("Confirmed: all the nodes have the same best tip")

        print("\nTesting if the current chain switched to the malicious chain")
        assert self.nodes[0].getbestblockhash() == last_malicious_blockhash
        print("Confirmed: malicious chain is the best chain")

        time.sleep(2)
        sync_mempools(self.nodes)

if __name__ == '__main__':
    blockdelay().main()
