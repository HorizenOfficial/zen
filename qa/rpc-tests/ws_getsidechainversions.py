#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import ForkHeights, BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, start_nodes, mark_logs
from test_framework.blockchainhelper import BlockchainHelper
from test_framework.wsproxy import JSONWSException

DEBUG_MODE = 1
NUMB_OF_NODES = 1

class ws_messages(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        common_args = [
            '-websocket=1', '-debug=ws',
            '-txindex=1',
            '-debug=py', '-debug=sc', '-debug=mempool', '-debug=net',
            '-debug=cert', '-scproofqueuesize=0', '-logtimemicros=1']

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args = [common_args] * NUMB_OF_NODES)

        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        Test the GetSidechainVersions websocket API
        '''

        test_helper = BlockchainHelper(self)

        # Node 0 generates blocks to reach the sidechain version fork point
        mark_logs("Node 1 generates {} block".format(ForkHeights['SC_VERSION']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['SC_VERSION'])
        self.sync_all()

        mark_logs("Node 0 creates sidechain 1 with version 0", self.nodes, DEBUG_MODE)
        test_helper.create_sidechain("sc1", 0)
        scid1 = test_helper.get_sidechain_id("sc1")

        mark_logs("Node 0 creates sidechain 2 with version 0", self.nodes, DEBUG_MODE)
        test_helper.create_sidechain("sc2", 0)
        scid2 = test_helper.get_sidechain_id("sc2")

        mark_logs("Node 0 generates 1 block to confirm the two sidechains", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Node 0 creates sidechain 3 with version 1", self.nodes, DEBUG_MODE)
        test_helper.create_sidechain("sc3", 1)
        scid3 = test_helper.get_sidechain_id("sc3")

        # Node 0 isn't considered from the WS GetSidechainVersions API since it's in the mempool (unconfirmed)
        mark_logs("Retrieve sidechain versions from node 0 (expecting invalid parameter for scid3)", self.nodes, DEBUG_MODE)
        try:
            sidechainVersions = self.nodes[0].ws_get_sidechain_versions([scid1, scid2, scid3])
            assert(False)
        except JSONWSException as e:
            assert("Invalid parameter" in e.error)

        mark_logs("Retrieve (confirmed) sidechain versions from node 0", self.nodes, DEBUG_MODE)
        try:
            sidechainVersions = self.nodes[0].ws_get_sidechain_versions([scid1, scid2])
        except JSONWSException as e:
            assert(False)

        mark_logs("Generate one block to confirm the sidechain 3", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Retrieve sidechain versions from node 0", self.nodes, DEBUG_MODE)
        try:
            sidechainVersions = self.nodes[0].ws_get_sidechain_versions([scid1, scid2, scid3])
        except JSONWSException as e:
            assert(False)

        for sidechainVersion in sidechainVersions:
            if (sidechainVersion['scId'] == scid3):
                assert_equal(sidechainVersion['version'], 1)
            else:
                assert_equal(sidechainVersion['version'], 0)

        mark_logs("Check that the command returns an error if we request more than 50 sidechains as input", self.nodes, DEBUG_MODE)
        try:
            self.nodes[0].ws_get_sidechain_versions([scid3] * 51)
            assert(False)
        except JSONWSException as e:
            assert("Invalid parameter" in e.error)

        mark_logs("Check that the command works with 50 sidechains as input", self.nodes, DEBUG_MODE)
        try:
            self.nodes[0].ws_get_sidechain_versions([scid3] * 50)
        except JSONWSException as e:
            assert(False)


if __name__ == '__main__':
    ws_messages().main()

