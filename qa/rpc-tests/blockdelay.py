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
class blockdelay(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 5)

    def setup_network(self, split=False):
        self.nodes = []

        # -exportdir option means we must provide a valid path to the destination folder for wallet backups
        ed0 = "-exportdir=" + self.options.tmpdir + "/node0"
        ed1 = "-exportdir=" + self.options.tmpdir + "/node1"
        ed2 = "-exportdir=" + self.options.tmpdir + "/node2"
        ed3 = "-exportdir=" + self.options.tmpdir + "/node3"
        self.nodes = start_nodes(4, self.options.tmpdir)


        if not split:
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:3])
            sync_mempools(self.nodes[1:3])

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 2, 3)
        self.is_network_split = split
        self.sync_all()

    def split_network(self):
        # Split the network of four nodes into nodes 0/1 and 2/3.
        assert not self.is_network_split
        disconnect_nodes(self.nodes[1], 2)
        disconnect_nodes(self.nodes[2], 1)
        self.is_network_split = True


    def join_network(self):
        #Join the (previously split) network halves together.
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 0, 3)
        connect_nodes_bi(self.nodes, 3, 0)
        #sync_blocks(self.nodes[0:3],1,True)
        #sync_mempools(self.nodes[1:3])
        self.sync_all()
        self.is_network_split = False

    def dump_ordered_tips(self, tip_list):
        sorted_x = sorted(tip_list, key=lambda k: k['status'])
        c = 0
        for y in sorted_x:
            if (c == 0):
                print(y)
            else:
                print(" ", y)
            c = 1

    def order_tips(self, tip_list):
        return sorted(tip_list, key=lambda k: k['status'])

    def mark_logs(self, msg):
        for x in self.nodes:
            x.dbg_log(msg)

    def sync_longest_fork(self, wait=1, limit_loop=0):

        '''
           Wait until all the nodes have the same length for the longest fork.
        '''
        rpc_connections = self.nodes
        loop_num = 0
        max_len = 0
        max_len_tot = 0

        while True:
            max_len_tot = 0

            if limit_loop > 0:
                loop_num += 1
                if loop_num > limit_loop:
                    break

            for x in rpc_connections:

                max_len = 0
                y = x.getchaintips()

                for j in y:
                    h = j['height']
                    max_len = max(max_len, h)

                max_len_tot += max_len

            if max_len_tot == max_len*len(rpc_connections):
                break

            time.sleep(wait)


    def run_test(self):
        blocks = []
        self.bl_count = 0

        blocks.append(self.nodes[0].getblockhash(0))
        print("\n\nGenesis block is: " + blocks[0])
#        raw_input("press enter to start..")
        try:
            print("\nChecking finality of block (%d) [%s]" % (0, blocks[0]))
            print("  Node0 has: %d" % self.nodes[0].getblockfinalityindex(blocks[0]))
            print
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)
        '''
        '''

        print("\n\nGenerating initial blockchain 104 blocks")
        blocks.extend(self.nodes[0].generate(101)) # block height 1
        #print(blocks[len(blocks)-1])
        self.sync_all()
        blocks.extend(self.nodes[1].generate(1)) # block height 2
        #print(blocks[len(blocks)-1])
        self.sync_all()
        blocks.extend(self.nodes[2].generate(1)) # block height 3
        #print(blocks[len(blocks)-1])
        self.sync_all()
        blocks.extend(self.nodes[3].generate(1)) # block height 4
        print("104) ", blocks[len(blocks)-1])
        self.sync_all()
        print("\nBlocks generated")

# Node(0): [0]->..->[104]
#   |
# Node(1): [0]->..->[104]
#   |
# Node(2): [0]->..->[104]
#   |
# Node(3): [0]->..->[104]

        print("\n\nSplit network")
        self.split_network()
        print("The network is split")

        print("\nNode0 sends 3.0 coins to Node1\n")
        # Node0 sends 3.0 coins to Node1
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 3.0)
        time.sleep(1)

        # Main chain
        print("\n\nGenerating 2 parallel chains with different length")

        bl = []
        print("\nGenerating 12 honest blocks")
        for i in range (0, 5):
            blocks.extend(self.nodes[0].generate(1)) # block height 105 -6 -7 -8 - 9 - 10
            bl.append(blocks[len(blocks)-1])
            print("%2d) %s" % ((i+105), bl[i]))
        self.sync_all()
        for i in range (5, 12):
            blocks.extend(self.nodes[1].generate(1)) # block height 111-12-13-14-15-16
            bl.append(blocks[len(blocks)-1])
            print("%2d) %s" % ((i+105), bl[i]))
        last_main_blockhash=blocks[len(blocks)-1]
        first_main_blockhash=bl[0]
        self.sync_all()
        print("Honest block generated")

        # check node1 balance has changed
        assert self.nodes[1].getbalance() == 3.0
        print("\nNode1 balance:", self.nodes[1].getbalance())

        assert self.nodes[0].getbestblockhash() == last_main_blockhash

#   Node(0): [0]->..->[104]->[105h]...->[116h]
#   /
# Node(1): [0]->..->[104]->[105h]...->[116h]
#
#
# Node(2): [0]->..->[104]
#   \
#   Node(3): [0]->..->[104]

        # Malicious nodes mining privately faster
        print("\nGenerating 13 malicious blocks")
        self.nodes[2].generate(10) # block height 105 - 6 -7 -8 -9-10 -11 12 13 14
        self.sync_all()
        self.nodes[3].generate(3) # block height 115 - 16 - 17
        self.sync_all()
        print("Malicious blocks generated")

        for i in range(0, 4):
            print("Node%d  ---" % i)
            getchaintips_res = self.nodes[i].getchaintips(True)
            self.dump_ordered_tips(getchaintips_res)
            assert(getchaintips_res[0]["penalty-at-start"] == 0)
            assert(getchaintips_res[0]["penalty-at-tip"] == 0)
            assert(getchaintips_res[0]["blocks-to-mainchain"] == 0)
            print("---")

#   Node(0): [0]->..->[104]->[105h]...->[116h]
#   /
# Node(1): [0]->..->[104]->[105h]...->[116h]
#
#
# Node(2): [0]->..->[104]->[105m]...->[117m]
#   \
#   Node(3): [0]->..->[104]->[105m]...->[117m]

        print("\n\nJoin network")
        self.mark_logs("Joining network")
#        raw_input("press enter to join the netorks..")
        self.join_network()
        self.sync_longest_fork(1, 10)

        print("\nNetwork joined")

        for i in range(0, 4):
            print("Node%d  ---" % i)
            getchaintips_res = self.nodes[i].getchaintips(True)
            self.dump_ordered_tips(getchaintips_res)
            print("---")

        print("\nTesting fork related data from getchaintips")
        print("\nTesting Node 0")
        getchaintips_res = self.order_tips(self.nodes[0].getchaintips(True))
        assert(getchaintips_res[1]["penalty-at-start"] == 11)
        assert(getchaintips_res[1]["penalty-at-tip"] == 65)
        assert(getchaintips_res[1]["blocks-to-mainchain"] == 65)
        assert(getchaintips_res[0]["penalty-at-start"] == 0)
        assert(getchaintips_res[0]["penalty-at-tip"] == 0)
        assert(getchaintips_res[0]["blocks-to-mainchain"] == 0)

        print("\nTesting Node 1")
        assert(self.nodes[1].getchaintips(True).sort(key=lambda tip: tip["height"]) == self.nodes[0].getchaintips(True).sort(key=lambda tip: tip["height"]))

        print("\nTesting Node 2")
        getchaintips_res = self.order_tips(self.nodes[2].getchaintips(True))
        assert(getchaintips_res[0]["penalty-at-start"] == 0)
        assert(getchaintips_res[0]["penalty-at-tip"] == 0)
        assert(getchaintips_res[0]["blocks-to-mainchain"] == 0)

        print("\nTesting Node 3")
        getchaintips_res = self.order_tips(self.nodes[3].getchaintips(True))
        assert(getchaintips_res[1]["penalty-at-start"] == 12)
        assert(getchaintips_res[1]["penalty-at-tip"] == 78)
        assert(getchaintips_res[1]["blocks-to-mainchain"] == 82)
        assert(getchaintips_res[0]["penalty-at-start"] == 0)
        assert(getchaintips_res[0]["penalty-at-tip"] == 0)
        assert(getchaintips_res[0]["blocks-to-mainchain"] == 0)

        print("\nTesting if the current chain is still the honest chain")
        assert self.nodes[0].getbestblockhash() == last_main_blockhash
        print("Confirmed: malicious chain is under penalty")

        print("\nChecking finality of first honest block [%s]" %  first_main_blockhash)
        for i in range(0, 4):
            try:
                print("  Node%d sees:"  % i)
                print("      finality: %d" % self.nodes[i].getblockfinalityindex(first_main_blockhash))
                print
            except JSONRPCException as e:
                errorString = e.error['message']
                print("      " + errorString)
                print

#   +-------Node(0): [0]->..->[104]->[105h]...->[116h]   <<==ACTIVE
#   |         /                  \
#   |        /                    +->[105m]...->[117m]
#   |       /
#   |     Node(1): [0]->..->[104]->[105h]...->[116h]   <<==ACTIVE
#   |                          \
#   |                           +->[105m]...->[117m]
#   |
#   |     Node(2): [0]->..->[104]->[105m]...->[117m]   <<==ACTIVE
#   |        \
#   |         \
#   |          \
#   +-------Node(3): [0]->..->[104]->[105m]...->[117m]   <<==ACTIVE
#                                \
#                                 +->[105h]...->[116h]

#        raw_input("press enter to generate 64 malicious blocks..")

        print("\nGenerating 64 malicious blocks")
        self.mark_logs("Generating 64 malicious blocks")
        self.nodes[3].generate(64)
        print("Malicious blocks generated")

        self.sync_longest_fork(1, 10)

        for i in range(0, 4):
            print("Node%d  ---" % i)
            self.dump_ordered_tips(self.nodes[i].getchaintips(True))
            print("---")

        print("\nTesting fork related data from getchaintips")
        print("\nTesting Node 0")
        getchaintips_res = self.order_tips(self.nodes[0].getchaintips(True))
        assert(getchaintips_res[1]["penalty-at-start"] == 11)
        assert(getchaintips_res[1]["penalty-at-tip"] == 1)
        assert(getchaintips_res[1]["blocks-to-mainchain"] == 1)
        assert(getchaintips_res[0]["penalty-at-start"] == 0)
        assert(getchaintips_res[0]["penalty-at-tip"] == 0)
        assert(getchaintips_res[0]["blocks-to-mainchain"] == 0)

        print("\nTesting Node 1")
        assert(self.nodes[1].getchaintips(True).sort(key=lambda tip: tip["height"]) == self.nodes[0].getchaintips(True).sort(key=lambda tip: tip["height"]))

        print("\nTesting Node 2")
        getchaintips_res = self.order_tips(self.nodes[2].getchaintips(True))
        assert(getchaintips_res[0]["penalty-at-start"] == 0)
        assert(getchaintips_res[0]["penalty-at-tip"] == 0)
        assert(getchaintips_res[0]["blocks-to-mainchain"] == 0)

        print("\nTesting Node 3")
        getchaintips_res = self.order_tips(self.nodes[3].getchaintips(True))
        assert(getchaintips_res[1]["penalty-at-start"] == 12)
        assert(getchaintips_res[1]["penalty-at-tip"] == 78)
        assert(getchaintips_res[1]["blocks-to-mainchain"] == 2290)
        assert(getchaintips_res[0]["penalty-at-start"] == 0)
        assert(getchaintips_res[0]["penalty-at-tip"] == 0)
        assert(getchaintips_res[0]["blocks-to-mainchain"] == 0)

        print("\nTesting if the current chain is still the honest chain")
        assert self.nodes[0].getbestblockhash() == last_main_blockhash
        print("Confirmed: malicious chain is under penalty")

        # check node1 balance has not changed
        assert self.nodes[1].getbalance() == 3.0
        print("\nNode1 balance is still the same:", self.nodes[1].getbalance())


#   +-------Node(0): [0]->..->[104]->[105h]...->[116h]                       <<==ACTIVE
#   |         /                  \
#   |        /                    +->[105m]...->[117m]->[118m]->..->[181m]
#   |       /
#   |     Node(1): [0]->..->[104]->[105h]...->[116h]                         <<==ACTIVE
#   |                          \
#   |                           +->[105m]...->[117m]->[118m]->..->[181m]
#   |
#   |     Node(2): [0]->..->[104]->[105m]...->[117m]->[118m]->..->[181m]     <<==ACTIVE
#   |        \
#   |         \
#   |          \
#   +-------Node(3): [0]->..->[104]->[105m]...->[117m]->[118m]->..->[181m]   <<==ACTIVE
#                                \
#                                 +->[105h]...->[116h]

#        raw_input("press enter to generate 65 honest blocks..")

        print("\nGenerating 65 more honest blocks")
        self.mark_logs("Generating 65 more honest blocks")
        self.nodes[0].generate(65)
        print("Honest blocks generated")
        sync_blocks(self.nodes, 1, True, 5)

#   +-------Node(0): [0]->..->[104]->[105h]...->[116h]->[117h]->..->[181h]   <<==ACTIVE
#   |         /                  \
#   |        /                    +->[105m]...->[117m]->[118m]->..->[181m]
#   |       /
#   |     Node(1): [0]->..->[104]->[105h]...->[116h]->[117h]->..->[181h]   <<==ACTIVE
#   |                          \
#   |                           +->[105m]...->[117m]->[118m]->..->[181m]
#   |
#   |     Node(2): [0]->..->[104]->[105m]...->[117m]->[118m]->..->[181m]   <<==ACTIVE
#   |        \
#   |         \
#   |          \
#   +-------Node(3): [0]->..->[104]->[105m]...->[117m]->[118m]->..->[181m]   <<==ACTIVE
#                                \
#                                 +->[105h]...->[116h]->[117h]->..->[181h]

        for i in range(0, 4):
            print("Node%d  ---" % i)
            self.dump_ordered_tips(self.nodes[i].getchaintips(True))
            print("---")

#        raw_input("press enter to generate 1 more malicious blocks which will cause the attack to succeed..")

        print("\nTesting fork related data from getchaintips")
        print("\nTesting Node 0")
        getchaintips_res = self.order_tips(self.nodes[0].getchaintips(True))
        assert(getchaintips_res[1]["penalty-at-start"] == 11)
        assert(getchaintips_res[1]["penalty-at-tip"] == 1)
        assert(getchaintips_res[1]["blocks-to-mainchain"] == 1)
        assert(getchaintips_res[0]["penalty-at-start"] == 0)
        assert(getchaintips_res[0]["penalty-at-tip"] == 0)
        assert(getchaintips_res[0]["blocks-to-mainchain"] == 0)

        print("\nTesting Node 1")
        assert(self.nodes[1].getchaintips(True).sort(key=lambda tip: tip["height"]) == self.nodes[0].getchaintips(True).sort(key=lambda tip: tip["height"]))

        print("\nTesting Node 2")
        getchaintips_res = self.order_tips(self.nodes[2].getchaintips(True))
        assert(getchaintips_res[0]["penalty-at-start"] == 0)
        assert(getchaintips_res[0]["penalty-at-tip"] == 0)
        assert(getchaintips_res[0]["blocks-to-mainchain"] == 0)

        print("\nTesting Node 3")
        getchaintips_res = self.order_tips(self.nodes[3].getchaintips(True))
        assert(getchaintips_res[1]["penalty-at-start"] == 12)
        assert(getchaintips_res[1]["penalty-at-tip"] == 2158)
        assert(getchaintips_res[1]["blocks-to-mainchain"] == 2158)
        assert(getchaintips_res[0]["penalty-at-start"] == 0)
        assert(getchaintips_res[0]["penalty-at-tip"] == 0)
        assert(getchaintips_res[0]["blocks-to-mainchain"] == 0)

        print("\nGenerating 1 more malicious block")
        self.mark_logs("Generating 1 more malicious block ")
        last_malicious_blockhash=self.nodes[3].generate(1)[0]
        print("Malicious block generated:")
        print(last_malicious_blockhash)

        print("\n\nWaiting that all network nodes are synced with same chain length")
        sync_blocks(self.nodes, 1, True, 5)

        print("Network nodes are synced")

        for i in range(0, 4):
            print("Node%d  ---" % i)
            self.dump_ordered_tips(self.nodes[i].getchaintips(True))
            print("---")

        print("\nTesting fork related data from getchaintips")
        print("\nTesting Node 0")
        getchaintips_res = self.order_tips(self.nodes[0].getchaintips(True))
        assert(getchaintips_res[1]["penalty-at-start"] == 0)
        assert(getchaintips_res[1]["penalty-at-tip"] == 0)
        assert(getchaintips_res[1]["blocks-to-mainchain"] == 4)
        assert(getchaintips_res[0]["penalty-at-start"] == 0)
        assert(getchaintips_res[0]["penalty-at-tip"] == 0)
        assert(getchaintips_res[0]["blocks-to-mainchain"] == 0)

        print("\nTesting Node 1")
        assert(self.nodes[1].getchaintips(True).sort(key=lambda tip: tip["height"]) == self.nodes[0].getchaintips(True).sort(key=lambda tip: tip["height"]))

        print("\nTesting Node 2")
        getchaintips_res = self.order_tips(self.nodes[2].getchaintips(True))
        assert(getchaintips_res[0]["penalty-at-start"] == 0)
        assert(getchaintips_res[0]["penalty-at-tip"] == 0)
        assert(getchaintips_res[0]["blocks-to-mainchain"] == 0)

        print("\nTesting Node 3")
        getchaintips_res = self.order_tips(self.nodes[3].getchaintips(True))
        assert(getchaintips_res[1]["penalty-at-start"] == 12)
        assert(getchaintips_res[1]["penalty-at-tip"] == 2158)
        assert(getchaintips_res[1]["blocks-to-mainchain"] == 2162)
        assert(getchaintips_res[0]["penalty-at-start"] == 0)
        assert(getchaintips_res[0]["penalty-at-tip"] == 0)
        assert(getchaintips_res[0]["blocks-to-mainchain"] == 0)

        print("\nTesting if all the nodes/chains have the same best tip")
        assert (self.nodes[0].getbestblockhash() == self.nodes[1].getbestblockhash()
                == self.nodes[2].getbestblockhash() == self.nodes[3].getbestblockhash())
        print("Confirmed: all the nodes have the same best tip")

        print("\nTesting if the current chain switched to the malicious chain")
        assert self.nodes[0].getbestblockhash() == last_malicious_blockhash
        print("Confirmed: malicious chain is the best chain")

#   +-------Node(0): [0]->..->[104]->[105h]...->[116h]->[117h]->..->[181h]
#   |         /                  \
#   |        /                    +->[105m]...->[117m]->[118m]->..->[181m]->[182m]  <<==ACTIVE!
#   |       /
#   |     Node(1): [0]->..->[104]->[105h]...->[116h]->[117h]->..->[181h]
#   |                          \
#   |                           +->[105m]...->[117m]->[118m]->..->[181m]->[182m]    <<==ACTIVE!
#   |
#   |     Node(2): [0]->..->[104]->[105m]...->[117m]->[118m]->..->[181m]->[182m]    <<==ACTIVE
#   |        \
#   |         \
#   |          \
#   +-------Node(3): [0]->..->[104]->[105m]...->[117m]->[118m]->..->[181m]->[182m]  <<==ACTIVE
#                                \
#                                 +->[105h]...->[116h]->[117h]->..->[181h]

        # check node1 balance has been erased
        assert self.nodes[1].getbalance() == 0.0
        print("\nNode1 balance has been erased!:", self.nodes[1].getbalance())

#        raw_input("press enter to connect a brand new node..")

        # Connect a fifth node from scratch and update
        s = "Connecting a new node"
        print(s)
        self.mark_logs(s)
        self.nodes.append(start_node(4, self.options.tmpdir))
        connect_nodes_bi(self.nodes, 4, 3)
        connect_nodes_bi(self.nodes, 3, 4)
        sync_blocks(self.nodes, 1, True, 5)

        for i in range(0, 5):
            print("Node%d  ---" % i)
            self.dump_ordered_tips(self.nodes[i].getchaintips(True))
            print("---")

        print("\nNode0 generating 1 new blocks")
        self.mark_logs("Node0 generating 1 new blocks")
        self.nodes[0].generate(1)
        print("New blocks generated")
        sync_blocks(self.nodes, 1, True, 5)

        for i in range(0, 5):
            print("Node%d  ---" % i)
            self.dump_ordered_tips(self.nodes[i].getchaintips(True))
            print("---")

        # check node1 balance has been restored
        assert self.nodes[1].getbalance() == 3.0
        print("Node1 balance has been restored: ", self.nodes[1].getbalance())

#   +-------Node(0): [0]->..->[104]->[105h]...->[116h]->[117h]->..->[181h]
#   |         /                  \
#   |        /                    +->[105m]...->[117m]->[118m]->..->[181m]->[182m]->[183m]  <<==ACTIVE!
#   |       /
#   |     Node(1): [0]->..->[104]->[105h]...->[116h]->[117h]->..->[181h]
#   |                          \
#   |                           +->[105m]...->[117m]->[118m]->..->[181m]->[182m]->[183m]    <<==ACTIVE!
#   |
#   |     Node(2): [0]->..->[104]->[105m]...->[117m]->[118m]->..->[181m]->[182m]->[183m]    <<==ACTIVE
#   |        \
#   |         \
#   |          \
#   +---------Node(3): [0]->..->[104]->[105m]...->[117m]->[118m]->..->[181m]->[182m]->[183m]  <<==ACTIVE
#               |                  \
#               |                   +->[105h]...->[116h]->[117h]->..->[181h]
#               |
#               |
#             Node(4): [0]->..- ..     ...     ...     ...     ...     ...      ... ->[183m]  <<==ACTIVE

if __name__ == '__main__':
    blockdelay().main()
