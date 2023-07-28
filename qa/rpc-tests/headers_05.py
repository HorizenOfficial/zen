#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import initialize_chain_clean, start_nodes, connect_nodes_bi
import time

class headers(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_nodes(self):
        self.nodes = start_nodes(3, self.options.tmpdir)

    def join_network(self):
        #Join the (previously split) network pieces together: 0-1-2
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 2, 1)
        self.sync_all()
        self.is_network_split = False

    def dump_ordered_tips(self, tip_list):
        sorted_x = sorted(tip_list, key=lambda k: k['status'])
        c = 0
        for y in sorted_x:
            if (c == 0):
                print(y)
            else:
                print(" ",y)
            c = 1

    def mark_logs(self, msg):
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)
        self.nodes[2].dbg_log(msg)

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

        print("\nNode1 generating 7 honest block")
        blocks.extend(self.nodes[1].generate(7)) # block height 2--8
        blA = blocks[2]
        blB = blocks[4]
        blC = blocks[6]
        for i in range(2, 9):
            print(blocks[i])
        self.sync_all()

        print("\nNode2 generating 20 mal block")
        blocks.extend(self.nodes[2].generate(20)) # block height 2--21
        for i in range(9, 29):
            print(blocks[i])
        self.sync_all()

# Node(0): [0]->[1]->[2h]->..->[8h]
#   |                   
# Node(1): [0]->[1]->[2h]->..->[8h]
#                       
# Node(2): [0]->[1]->[2m]->..->..->[21m]

        print("\n\nJoin network")
        self.mark_logs("Joining network")
        self.join_network()

        print("\nNetwork joined") 
        self.mark_logs("Network joined")

        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print("---")

# Node(0): [0]->[1]->[2h]->..->[8h]       **Active**    
#   |             \     
#   |              +->[2m]->..->..->[21m]    
#   |                   
# Node(1): [0]->[1]->[2h->..->[8h]        **Active**    
#   |             \     
#   |              +->[2m]->..->..->[21m]    
#   |                   
#   |                   
# Node(2): [0]->[1]->[2m]->..->..->[21m]  **Active**    
#                 \     
#                  +->[2h]->..->[8h]

        print("Checking finality of block[", blA, "]")
        print("  Node0 has: %d" % self.nodes[0].getblockfinalityindex(blA))
        print("  Node1 has: %d" % self.nodes[1].getblockfinalityindex(blA))
        print("Checking finality of block[", blB, "]")
        print("  Node0 has: %d" % self.nodes[0].getblockfinalityindex(blB))
        print("  Node1 has: %d" % self.nodes[1].getblockfinalityindex(blB))
        print("Checking finality of block[", blC, "]")
        print("  Node0 has: %d" % self.nodes[0].getblockfinalityindex(blC))
        print("  Node1 has: %d" % self.nodes[1].getblockfinalityindex(blC))

        for j in range(1, 10):
            print("### block %d ---" % j)
            print("Node2 generating 1 mal block")
            blocks.extend(self.nodes[2].generate(1)) # block height 8--12
            n=len(blocks)-1
            print(blocks[n])
            time.sleep(3)

            try:
                print("Checking finality of block[", blA, "]")
                print("  Node0 has: %d" % self.nodes[0].getblockfinalityindex(blA))
                print("  Node1 has: %d" % self.nodes[1].getblockfinalityindex(blA))
                print("Checking finality of block[", blB, "]")
                print("  Node0 has: %d" % self.nodes[0].getblockfinalityindex(blB))
                print("  Node1 has: %d" % self.nodes[1].getblockfinalityindex(blB))
                print("Checking finality of block[", blC, "]")
                print("  Node0 has: %d" % self.nodes[0].getblockfinalityindex(blC))
                print("  Node1 has: %d" % self.nodes[1].getblockfinalityindex(blC))
            except JSONRPCException as e:
                errorString = e.error['message']
                print(errorString)
                print("\n ===> Malicious attach succeeded after %d blocks!!\n\n" % j)
                break

        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print("---")

if __name__ == '__main__':
    headers().main()
