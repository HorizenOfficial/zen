#!/usr/bin/env python3
# Copyright (c) 2016 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


from test_framework.blockchainhelper import EXPECT_SUCCESS, BlockchainHelper, SidechainParameters
from test_framework.test_framework import BitcoinTestFramework, ForkHeights
from test_framework.util import assert_equal, assert_greater_than, initialize_chain_clean, mark_logs, \
                                start_nodes, sync_blocks, wait_and_assert_operationid_status, wait_until

DEBUG_MODE = 1
NUMB_OF_NODES = 2
Z_FEE = 0.0001

class Test (BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_nodes(self):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, [['-experimentalfeatures', '-zmergetoaddress', '-limitdebuglogsize=false']] * NUMB_OF_NODES)

    def run_test (self):

        '''
        In this test we want to check that a tx becoming irrevocably invalid after hard-fork overcome
        is actually removed from mempool.

        The test is composed of two sections:
        1. Reach sidechains fork, create a sidechain, disconnect one block and check that the sidechain creation is evicted
        2. Reach a height close to the shielded pool deprecation fork, create a shielding tx, mine one block, check that the tx is evicted
        '''

        # ####################################################################################################
        # Fork 8 - Sidechain hard fork
        # ####################################################################################################
        mark_logs("********** Fork 8 - Sidechain hard fork **********", self.nodes, DEBUG_MODE)

        mark_logs("Node 0 starts mining to reach the block immediately before the sidechain hard fork activation", self.nodes, DEBUG_MODE)
        sc_fork_height = ForkHeights['MINIMAL_SC']
        blocks = self.nodes[0].generate(sc_fork_height - 1) # The mempool should accept sidechain txs one block before the hard fork
        self.sync_all()

        mark_logs("Node 0 creates a v0 sidechain", self.nodes, DEBUG_MODE)
        test_helper = BlockchainHelper(self)
        test_helper.create_sidechain("v0_sidechain", SidechainParameters["DEFAULT_SC_V0"], EXPECT_SUCCESS)
        self.sync_all()

        mark_logs("Check that the nodes discard the sidechain transaction after disconnecting one block", self.nodes, DEBUG_MODE)
        for node in self.nodes:
            assert_equal(len(node.getrawmempool()), 1)              # All nodes should have the sidechain creation transaction
            node.invalidateblock(blocks[-1])                        # Let's disconnect the last block
            assert_equal(node.getblockcount(), sc_fork_height - 2)  # At this point, the sidechain transaction can't be mined anymore
            assert_equal(len(node.getrawmempool()), 0)              # Nodes should have evicted the sidechain creation transaction

        mark_logs("RemoveStaleTransaction() function worked properly for block disconnection on a hard fork", self.nodes, DEBUG_MODE)





        # Preparation for fork12: some shielded notes are created (needed later, but must be prepared before fork11 occurs)
        notes_to_create = 3
        for i in range(notes_to_create):
            opid = self.nodes[0].z_mergetoaddress(["ANY_TADDR"], self.nodes[0].z_getnewaddress(), Z_FEE, 2, 0)["opid"]
            wait_and_assert_operationid_status(self.nodes[0], opid)
            self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()

        # ####################################################################################################
        # Fork 11 - Shielded pool deprecation hard fork
        # ####################################################################################################
        # ####################################################################################################
        # Fork 12 - Unshielding to script only hard fork
        # ####################################################################################################
        for fork in range(11, 12 + 1):
            if (fork == 11):
                fork_descr = "Shielded Pool Deprecation"
                fork_height = ForkHeights['SHIELDED_POOL_DEPRECATION']
                tx_type_descr = "shielding transaction"
            elif (fork == 12):
                fork_descr = "Unshielding To Script Only"
                fork_height = ForkHeights['UNSHIELDING_TO_SCRIPT_ONLY']
                tx_type_descr = "unshielding transaction to publick-key-address"

            mark_logs(f"********** Fork {fork} - {fork_descr} hard fork **********", self.nodes, DEBUG_MODE)

            mark_logs(f"Node 0 starts mining to get close to the {fork_descr} hard fork [Fork - 3]", self.nodes, DEBUG_MODE)
            block_count = self.nodes[0].getblockcount()
            self.nodes[1].generate(fork_height - block_count - 3)
            self.sync_all()

            mark_logs("Split the network", self.nodes, DEBUG_MODE)
            self.split_network(0)

            mark_logs(f"Node 0 creates {tx_type_descr}", self.nodes, DEBUG_MODE)
            if (fork == 11):
                total_funds_before = sum(x['amount'] for x in self.nodes[0].listunspent(0))
                opid = self.nodes[0].z_mergetoaddress(["ANY_TADDR"], self.nodes[0].z_getnewaddress(), Z_FEE, 2, 0)["opid"]
            elif (fork == 12):
                total_funds_before = sum(x['amount'] for x in self.nodes[0].z_listunspent(0))
                opid = self.nodes[0].z_mergetoaddress(["ANY_ZADDR"], self.nodes[0].getnewaddress(), Z_FEE, 0, 2)["opid"]
            wait_and_assert_operationid_status(self.nodes[0], opid)

            # Give time to the wallet to be notified and updated the balance (listunspent would otherwise return an outdated value)
            wait_until(lambda: self.nodes[0].getmempoolinfo()['fullyNotified'] == True, 20)
            if (fork == 11):
                assert_greater_than(total_funds_before, sum(x['amount'] for x in self.nodes[0].listunspent(0))) # Some less funds available
            elif (fork == 12):
                assert_greater_than(total_funds_before, sum(x['amount'] for x in self.nodes[0].z_listunspent(0))) # Some less funds available

            mark_logs("Join the network", self.nodes, DEBUG_MODE)
            self.join_network(0)

            mark_logs(f"Check that the {tx_type_descr} is only available on node 0 (no rebroadcasting happens)", self.nodes, DEBUG_MODE)
            assert_equal(self.nodes[0].getmempoolinfo()["size"], 1)
            assert_equal(self.nodes[1].getmempoolinfo()["size"], 0)

            mark_logs("Node 1 mines one block, new height is [Fork - 2]", self.nodes, DEBUG_MODE)
            self.nodes[1].generate(1)
            sync_blocks(self.nodes)

            mark_logs("Check that the mempool situation is unchanged", self.nodes, DEBUG_MODE)
            assert_equal(self.nodes[0].getblockcount(), fork_height - 2)
            assert_equal(self.nodes[0].getmempoolinfo()["size"], 1)
            assert_equal(self.nodes[1].getmempoolinfo()["size"], 0)

            mark_logs(f"Node 1 mines another block, new height is [Fork - 1] ({tx_type_descr} can't be mined in the next block)", self.nodes, DEBUG_MODE)
            self.nodes[1].generate(1)
            sync_blocks(self.nodes)

            mark_logs(f"Check that the {tx_type_descr} has been evicted", self.nodes, DEBUG_MODE)
            assert_equal(self.nodes[0].getblockcount(), fork_height - 1)
            assert_equal(self.nodes[0].getmempoolinfo()["size"], 0)
            assert_equal(self.nodes[1].getmempoolinfo()["size"], 0)

            mark_logs(f"Check that the {tx_type_descr} is not rebroadcasted even in case of explicit request", self.nodes, DEBUG_MODE)
            resenttxs = self.nodes[0].resendwallettransactions()
            self.sync_all()
            assert_equal(len(resenttxs), 0)

            mark_logs(f"Check that the funds used for the {tx_type_descr} are available again", self.nodes, DEBUG_MODE)
            if (fork == 11):
                assert_equal(sum(x['amount'] for x in self.nodes[0].listunspent(0)), total_funds_before)
                self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), total_funds_before, "", "", True)
            elif (fork == 12):
                assert_equal(sum(x['amount'] for x in self.nodes[0].z_listunspent(0)), total_funds_before)
                notes_quantity = len(self.nodes[0].z_listunspent(0))
                opid = self.nodes[0].z_mergetoaddress(["ANY_ZADDR"], self.nodes[0].z_getnewaddress(), Z_FEE, 0, notes_quantity)["opid"]
                wait_and_assert_operationid_status(self.nodes[0], opid)
            assert_equal(self.nodes[0].getmempoolinfo()["size"], 1)
            self.sync_all()

            mark_logs("RemoveStaleTransaction() function worked properly for block connection on a hard fork", self.nodes, DEBUG_MODE)


if __name__ == '__main__':
    Test().main()