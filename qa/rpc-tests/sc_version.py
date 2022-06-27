#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework, ForkHeights
from test_framework.util import initialize_chain_clean, start_nodes,mark_logs
from test_framework.blockchainhelper import BlockchainHelper, EXPECT_SUCCESS, EXPECT_FAILURE

NUMB_OF_NODES = 1
DEBUG_MODE = 1

class sc_version(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[['-logtimemicros=1', '-debug=sc', '-scproofqueuesize=0', 
                                              '-debug=py', '-debug=mempool', '-debug=net', '-debug=cert',
                                              '-debug=bench']] * NUMB_OF_NODES)

        self.is_network_split = split

    def run_test(self):
        '''
        Tests the management of the "sidechain version" field in the "sc_create" RPC command.
        In particular:
            - before sidechain version fork point (height = 450) v0 must be accepted while v1 must be rejected
            - after sidechain version fork point (height = 450) both v0 and v1 must be accepted
        '''

        test_helper = BlockchainHelper(self)

        # We need to stop at 448 (so that the sidechain would be created in block 449)
        mark_logs("Node 0 generates {} blocks".format(ForkHeights['SC_VERSION'] - 2), self.nodes,DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['SC_VERSION'] - 2)
        self.sync_all()

        mark_logs("Node 0 creates a v0 sidechain", self.nodes, DEBUG_MODE)
        test_helper.create_sidechain("pre_fork_v0", 0, EXPECT_SUCCESS)

        mark_logs("Node 0 creates a v1 sidechain (expecting failure)", self.nodes, DEBUG_MODE)
        assert("Invalid sidechain version" in test_helper.create_sidechain("pre_fork_v1", 1, EXPECT_FAILURE))

        mark_logs("Node 0 creates a v2 sidechain (expecting failure)", self.nodes, DEBUG_MODE)
        assert("Invalid sidechain version" in test_helper.create_sidechain("pre_fork_v2", 2, EXPECT_FAILURE))

        self.sync_all()

        # Generate 1 more block to reach the sidechain version fork point
        mark_logs("Node 0 generates 1 block (to reach height {})".format(ForkHeights['SC_VERSION'] - 1), self.nodes,DEBUG_MODE)
        self.nodes[0].generate(1)

        mark_logs("Node 0 creates a v0 sidechain", self.nodes, DEBUG_MODE)
        test_helper.create_sidechain("post_fork_v0", 0, EXPECT_SUCCESS)

        mark_logs("Node 0 creates a v1 sidechain", self.nodes, DEBUG_MODE)
        test_helper.create_sidechain("post_fork_v1", 1, EXPECT_SUCCESS)

        mark_logs("Node 0 creates a v2 sidechain (expecting failure)", self.nodes, DEBUG_MODE)
        assert("Invalid sidechain version" in test_helper.create_sidechain("post_fork_v2", 2, EXPECT_FAILURE))

        self.sync_all()


if __name__ == '__main__':
    sc_version().main()
