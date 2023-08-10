#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import initialize_chain_clean, start_nodes, mark_logs, \
    assert_equal, colorize as cc
from headers_common import print_ordered_tips

DEBUG_MODE = 1

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

    def run_test(self):
        blocks = []

        blocks.append(self.nodes[0].getblockhash(0))
        mark_logs(cc('e', "Genesis block is:         ") + blocks[0], self.nodes, DEBUG_MODE, color='n')

        blocks.extend(self.nodes[0].generate(1)) # block height 1
        mark_logs(cc('c', "Node 0 generated a block: ") + blocks[len(blocks)-1], self.nodes, DEBUG_MODE, color='n')

        mark_logs("Before sync", self.nodes, DEBUG_MODE, color='e')
        self.sync_all()
        mark_logs("After sync", self.nodes, DEBUG_MODE, color='e')

        mark_logs("Check that nodes are sync'ed - same height", self.nodes, DEBUG_MODE, color='g')
        assert_equal(self.nodes[0].getblockcount(), 1)
        assert_equal(self.nodes[1].getblockcount(), 1)
        mark_logs("Check that nodes are sync'ed - tip pointing to the same block", self.nodes, DEBUG_MODE, color='g')
        node0ActiveTip = [tip for tip in self.nodes[0].getchaintips() if tip['status'] == 'active'][0]
        node1ActiveTip = [tip for tip in self.nodes[1].getchaintips() if tip['status'] == 'active'][0]
        assert(node0ActiveTip == node1ActiveTip)

        print_ordered_tips(self.nodes)
# Node(0): [0]->[1]
#   |
# Node(1): [0]->[1]

if __name__ == '__main__':
    headers().main()
