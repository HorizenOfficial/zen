#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test -alertnotify 
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.util import start_node, connect_nodes, assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
    dump_sc_info_record
from test_framework.authproxy import JSONRPCException

import os

NUMB_OF_NODES = 3
DEBUG_MODE = 1

BLOCK_VERSION = 4
UP_VERSION = 211
ILLEGAL_VERSION = 2

# post sidechain fork
SC_CERTIFICATE_BLOCK_VERSION = 3


class ForkNotifyTest(BitcoinTestFramework):

    alert_filename = None  # Set by setup_network

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self):
        self.nodes = []
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file
        self.nodes.append(start_node(0, self.options.tmpdir,
                            ["-blockversion=%d" % ILLEGAL_VERSION, "-alertnotify=echo %s >> \"" + self.alert_filename + "\""]))
        # Node1 mines block.version=211 blocks
        self.nodes.append(start_node(1, self.options.tmpdir,
                                ["-blockversion=%d" % UP_VERSION]))
        connect_nodes(self.nodes[1], 0)

        self.nodes.append(start_node(2, self.options.tmpdir, []))
        connect_nodes(self.nodes[2], 1)

        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        
        mark_logs("Node 2 generates 150 block", self.nodes, DEBUG_MODE)
        self.nodes[2].generate(150)
        self.sync_all()

        # check block versions ( skip genesis that is 4)
        mark_logs(("Check that block version is %d" % BLOCK_VERSION), self.nodes, DEBUG_MODE)
        for i in range (1, 151):
            v = self.nodes[0].getblock(str(i))['version']
            assert_equal(v, BLOCK_VERSION)

        # Mine 51 up-version blocks, they are supported up to sidechain fork
        mark_logs("Node 1 generates 51 block with an up-version", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(51)
        self.sync_all()

        # check block versions 
        mark_logs(("Check that block version is %d" % UP_VERSION), self.nodes, DEBUG_MODE)
        for i in range (151, 201):
            v = self.nodes[0].getblock(str(i))['version']
            assert_equal(v, UP_VERSION)

        try:
            self.nodes[0].generate(1)
            raise AssertionError("Can not generate blocks with version = 2")
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString + " ===> Ok, Can not generate blocks with version = 2", self.nodes, DEBUG_MODE)

        # reach sidechain fork height
        fork_height = ForkHeights['MINIMAL_SC']
        active_height =  self.nodes[2].getblockcount()
        delta = fork_height - active_height

        if delta > 0:
            mark_logs(("Node 2 generates %d block to reach sidechain fork" % delta), self.nodes, DEBUG_MODE)
            self.nodes[2].generate(delta)
            self.sync_all()

        active_height =  self.nodes[2].getblockcount()
        mark_logs(("Chain height = %d" % active_height), self.nodes, DEBUG_MODE)

        # check last block version
        mark_logs(("Check that best block version is %d" % SC_CERTIFICATE_BLOCK_VERSION), self.nodes, DEBUG_MODE)
        v = self.nodes[0].getblock(str(active_height))['version']
        assert_equal(v, SC_CERTIFICATE_BLOCK_VERSION)
        
        # check that version 211 can not be used anymore after fork
        try:
            self.nodes[1].generate(1)
            raise AssertionError("Can not generate blocks with version = 211")
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString + " ===> Ok, Can not generate blocks with version = 211", self.nodes, DEBUG_MODE)

        
if __name__ == '__main__':
    ForkNotifyTest().main()
