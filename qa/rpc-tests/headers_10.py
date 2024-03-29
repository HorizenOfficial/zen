#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import initialize_chain_clean, start_nodes, sync_blocks
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

        fork_size=160
#        fork_size=3
        print("\nNode1 generating %d honest block" % fork_size)
        blocks.extend(self.nodes[1].generate(fork_size)) # block height 2
        print(blocks[-1])
        self.sync_all()

        print("\nNode2 generating %d mal block" % fork_size)
        blocks.extend(self.nodes[2].generate(fork_size)) # block height 2
        print(blocks[-1])
        self.sync_all()

        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print("---")

# Node(0): [0]->[1]->[2h]
#   |                   
# Node(1): [0]->[1]->[2h]
#                       
# Node(2): [0]->[1]->[2m]

        print("\n\nJoin network")
        self.mark_logs("Joining network")
        self.join_network()

        sync_blocks(self.nodes, 1, True, 15)

        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print("---")

        print("\nNetwork joined") 
        self.mark_logs("Network joined")

        '''
        try:
            print "\nChecking finality of block (%d) [%s]" % (0, blocks[2])
            print "  Node0 has: %d" % self.nodes[0].getblockfinalityindex(blocks[2])
            print
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)
            '''

        print("\nNode2 generating 1 mal block")
        blocks.extend(self.nodes[2].generate(1)) # block height 2
        print(blocks[-1])
        sync_blocks(self.nodes, 1, True, 3)
#        self.sync_all()
#        blocks.extend(self.nodes[2].generate(1)) # block height 2
#        sync_blocks(self.nodes, 1, True, 3)
#        time.sleep(10)

# Node(0): [0]->[1]->[2h]
        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print("---")

        '''
        try:
            print "\nChecking finality of block (%d) [%s]" % (0, blocks[2])
            print "  Node0 has: %d" % self.nodes[0].getblockfinalityindex(blocks[2])
            print
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)
            '''

#        print("\nNode2 generating 1 mal block")
#        blocks.extend(self.nodes[2].generate(1)) # block height 2
#        print(blocks[3])
#        sync_blocks(self.nodes, 1, True, 3)
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

if __name__ == '__main__':
    headers().main()
