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
        initialize_chain_clean(self.options.tmpdir, 2)

    def setup_nodes(self):
        self.nodes = start_nodes(2, self.options.tmpdir,
            extra_args = [
                ["-debug=1", "-logtimemicros=1"],
                ["-debug=1", "-logtimemicros=1"]
            ])

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
        print("\n\nGenesis block is:\n" + blocks[0] + "\n")
        # raw_input("press enter to go on..")

        s = "Node 0 generates a block"
        print("\n" + s) 
        self.nodes[0].dbg_log(s)
        self.nodes[1].dbg_log(s)

        blocks.extend(self.nodes[0].generate(1)) # block height 1
        print(blocks[len(blocks)-1])

        self.nodes[0].dbg_log("Before sync")
        self.nodes[1].dbg_log("Before sync")

        self.sync_all()

        self.nodes[0].dbg_log("After sync")
        self.nodes[1].dbg_log("After sync")

#        raw_input("press enter to go on..")
        print

        for i in range(0, 2):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print("---")

#        raw_input("press enter to go on..")

if __name__ == '__main__':
    headers().main()
