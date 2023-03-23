#!/usr/bin/env python3
# Copyright (c) 2016 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


from test_framework.test_framework import BitcoinTestFramework, ForkHeights
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_greater_than, initialize_chain_clean, \
    start_nodes, stop_nodes, wait_bitcoinds, connect_nodes, wait_and_assert_operationid_status

import sys
from decimal import Decimal

RPC_HARD_FORK_DEPRECATION = -40

class ShieldedPoolDeprecationTest (BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 2)

    def setup_network(self, split=False):
        args = ['-debug=zrpcunsafe', '-experimentalfeatures', '-zmergetoaddress']
        self.nodes = start_nodes(2, self.options.tmpdir, [['-debug=zrpcunsafe', '-experimentalfeatures', '-zmergetoaddress']] * 2)
        connect_nodes(self.nodes[0],1)
        self.is_network_split=False
        self.sync_all()

    def run_test (self):

        ForkHeight = ForkHeights['SHIELDED_POOL_DEPRECATION']

        print("Mining blocks...")

        self.nodes[0].generate(ForkHeight - 10)
        self.sync_all()

        node0_taddr0 = self.nodes[0].getnewaddress()
        node0_zaddr0 = self.nodes[0].z_getnewaddress()

        # first round pre-fork, second round post-fork
        pre_fork_round = 0
        post_fork_round = 1
        for round in range(2):
            try:
                # shielding coinbase
                opid = self.nodes[0].z_shieldcoinbase(node0_taddr0, node0_zaddr0, 0.0001, 2)["opid"]
                if (round == post_fork_round):
                    assert(False)
                wait_and_assert_operationid_status(self.nodes[0], opid)
            except JSONRPCException as e:
                if (round == pre_fork_round):
                    print("Unexpected exception caught during testing: " + str(sys.exc_info()[0]))
                    assert(False)
                else:
                    print("Expected exception caught during testing due to deprecation (error=" + str(e.error["code"]) + ")")
                    assert_equal(e.error["code"], RPC_HARD_FORK_DEPRECATION)

            try:
                # moving transparent funds between node0 t-addresses (before merging)
                node0_taddr1 = self.nodes[0].getnewaddress()
                node0_taddr2 = self.nodes[0].getnewaddress()
                self.nodes[0].generate(1)
                self.sync_all()
                self.nodes[0].sendmany("", {node0_taddr1: 1.0, node0_taddr2: 1.0})
                # merging transparent funds to z-address (shielding)
                opid = self.nodes[0].z_mergetoaddress(["ANY_TADDR"], node0_zaddr0, 0.0001, 2)["opid"]
                if (round == post_fork_round):
                    assert(False)
                wait_and_assert_operationid_status(self.nodes[0], opid)
            except JSONRPCException as e:
                if (round == pre_fork_round):
                    print("Unexpected exception caught during testing: " + str(sys.exc_info()[0]))
                    assert(False)
                else:
                    print("Expected exception caught during testing due to deprecation (error=" + str(e.error["code"]) + ")")
                    assert_equal(e.error["code"], RPC_HARD_FORK_DEPRECATION)

            try:
                # moving transparent funds between node0 t-addresses (before sending to z-address)
                self.nodes[0].generate(1)
                self.sync_all()
                self.nodes[0].sendmany("", {node0_taddr1: 1.0})
                # sending transparent funds to z-address (shielding)
                opid = self.nodes[0].z_sendmany(node0_taddr1, [{"address":node0_zaddr0, "amount": 1.0}], 0, 0)
                if (round == post_fork_round):
                    assert(False)
                wait_and_assert_operationid_status(self.nodes[0], opid)
            except JSONRPCException as e:
                if (round == pre_fork_round):
                    print("Unexpected exception caught during testing: " + str(sys.exc_info()[0]))
                    assert(False)
                else:
                    print("Expected exception caught during testing due to deprecation (error=" + str(e.error["code"]) + ")")
                    assert_equal(e.error["code"], RPC_HARD_FORK_DEPRECATION)

            if (round == pre_fork_round):
                blockcount = self.nodes[0].getblockcount()
                if (blockcount < ForkHeight):
                    self.nodes[0].generate(ForkHeight - blockcount)
                    self.sync_all()

        return


if __name__ == '__main__':
    ShieldedPoolDeprecationTest().main()
