#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Base class for RPC testing

# Add python-bitcoinrpc to module search path:
import os
import sys

import shutil
import tempfile
import traceback
from enum import Enum, auto

from test_framework.authproxy import JSONRPCException
from test_framework.wsproxy import JSONWSException
from test_framework.util import assert_equal, check_json_precision, \
    initialize_chain, initialize_chain_clean, \
    start_nodes, connect_nodes_bi, stop_nodes, \
    sync_blocks, sync_mempools, wait_bitcoinds, \
    disconnect_nodes

MINER_REWARD_POST_H200 = 7.50

ForkHeights = {
    'MINIMAL_SC':                  420,
    'SC_VERSION':                  450,
    'NON_CEASING_SC':              480,
    'SHIELDED_POOL_DEPRECATION':   990,
    'SHIELDED_POOL_REMOVAL':       1010,
}

class syncType(Enum):
    SYNC_BLOCKS = auto()
    SYNC_MEMPOOLS = auto()
    SYNC_ALL = auto()

class BitcoinTestFramework(object):

    def __init__(self):
        self.nodes = []
        self.is_network_split = False
        self.numb_of_nodes = 0
        self.current_mock_time = 0

    # These may be over-ridden by subclasses:
    def run_test(self):
        for node in self.nodes:
            assert_equal(node.getblockcount(), 200)
            assert_equal(node.getbalance(), 25 * 11.4375)

    def add_options(self, parser):
        pass

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain(self.options.tmpdir)

    def setup_nodes(self):
        self.nodes = start_nodes(4, self.options.tmpdir)

    def setup_network(self):
        # setup_network by default provides linear topology ep. 1 - 2 - 3 - ... - N
        self.setup_nodes()
        self.numb_of_nodes = len(self.nodes)

        for idx, _ in enumerate(self.nodes):
            if idx < (self.numb_of_nodes-1):
                connect_nodes_bi(self.nodes, idx, idx+1)

        self.is_network_split = False
        self.sync_all()       

    def split_network(self, id = 1):
        # Split the network of between adjanced nodes in linear topology ep. nodes 0-1 and 2-3.
        assert not self.is_network_split
        disconnect_nodes(self.nodes, id, id + 1)
        disconnect_nodes(self.nodes, id + 1, id)
        self.is_network_split = True

    def sync_all(self):
        if self.is_network_split:
            sync_blocks(self.nodes[:2])
            sync_blocks(self.nodes[2:])
            sync_mempools(self.nodes[:2])
            sync_mempools(self.nodes[2:])
        else:
            sync_blocks(self.nodes)
            sync_mempools(self.nodes)

    def join_network(self, id = 1, sync_type = syncType.SYNC_BLOCKS):
        # Join the (previously split) adjanced nodes in linear network together: 0-1 >-< 2-3
        assert self.is_network_split
        connect_nodes_bi(self.nodes, id, id + 1)
        if sync_type == syncType.SYNC_BLOCKS or sync_type == syncType.SYNC_ALL:
            sync_blocks([self.nodes[id], self.nodes[id + 1]])
        if sync_type == syncType.SYNC_MEMPOOLS or sync_type == syncType.SYNC_ALL:
            sync_mempools([self.nodes[id], self.nodes[id + 1]])
        self.is_network_split = False

    def set_mock_time(self, time: int):
        '''
        Sets the mock time for all the nodes in the test.

        Args:
            time (int): absolute mock time to set (seconds)
        '''
        self.current_mock_time = time

        for node in self.nodes:
            node.setmocktime(time)

    def increase_mock_time(self, increment: int):
        '''
        Increase the current mock time by "increment" seconds.

        Args:
            increment (int): relative mock time to set as increment (seconds)
        '''
        self.set_mock_time(self.current_mock_time + increment)


    def main(self):
        import optparse

        parser = optparse.OptionParser(usage="%prog [options]")
        parser.add_option("--nocleanup", dest="nocleanup", default=False, action="store_true",
                          help="Leave bitcoinds and test.* datadir on exit or error")
        parser.add_option("--noshutdown", dest="noshutdown", default=False, action="store_true",
                          help="Don't stop bitcoinds after the test execution")
        parser.add_option("--srcdir", dest="srcdir", default="../../src",
                          help="Source directory containing bitcoind/bitcoin-cli (default: %default)")
        parser.add_option("--tmpdir", dest="tmpdir", default=tempfile.mkdtemp(prefix="test"),
                          help="Root directory for datadirs")
        parser.add_option("--tracerpc", dest="trace_rpc", default=False, action="store_true",
                          help="Print out all RPC calls as they are made")
        self.add_options(parser)
        (self.options, self.args) = parser.parse_args()

        if self.options.trace_rpc:
            import logging
            logging.basicConfig(level=logging.DEBUG)

        os.environ['PATH'] = self.options.srcdir+":"+os.environ['PATH']

        check_json_precision()

        success = False
        try:
            if not os.path.isdir(self.options.tmpdir):
                os.makedirs(self.options.tmpdir)
            self.setup_chain()

            self.setup_network()

            self.run_test()

            success = True

        except JSONRPCException as e:
            print("JSONRPC error: "+e.error['message'])
            traceback.print_tb(sys.exc_info()[2])
        except JSONWSException as e:
            print("JSONWS error: {}".format(e.error))
            traceback.print_tb(sys.exc_info()[2])
        except AssertionError as e:
            print("Assertion failed: " + str(e))
            traceback.print_tb(sys.exc_info()[2])
        except Exception as e:
            print("Unexpected exception caught during testing: " + str(e))
            traceback.print_tb(sys.exc_info()[2])

        if not self.options.noshutdown and len(self.nodes) > 0:
            print("Stopping nodes")
            stop_nodes(self.nodes)
            wait_bitcoinds()
        else:
            print("Note: bitcoinds were not stopped and may still be running")

        if not self.options.nocleanup and not self.options.noshutdown:
            print("Cleaning up")
            shutil.rmtree(self.options.tmpdir)

        if success:
            print("Tests successful")
            sys.exit(0)
        else:
            print("Failed")
            sys.exit(1)


# Test framework for doing p2p comparison testing, which sets up some bitcoind
# binaries:
# 1 binary: test binary
# 2 binaries: 1 test binary, 1 ref binary
# n>2 binaries: 1 test binary, n-1 ref binaries

class ComparisonTestFramework(BitcoinTestFramework):

    # Can override the num_nodes variable to indicate how many nodes to run.
    def __init__(self):
        self.num_nodes = 2

    def add_options(self, parser):
        parser.add_option("--testbinary", dest="testbinary",
                          default=os.getenv("BITCOIND", "zend"),
                          help="zend binary to test")
        parser.add_option("--refbinary", dest="refbinary",
                          default=os.getenv("BITCOIND", "zend"),
                          help="zend binary to use for reference nodes (if any)")

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, self.num_nodes)

    def setup_network(self):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir,
                                    extra_args=[['-logtimemicros=1', '-debug=net', '-debug=rpc', '-whitelist=127.0.0.1']] * self.num_nodes,
                                    binary=[self.options.testbinary] +
                                           [self.options.refbinary]*(self.num_nodes-1))
