#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import ForkHeights, BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, start_nodes, mark_logs

NUMB_OF_NODES = 1
DEBUG_MODE = 1

class FundAddressesTest(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir)

        self.is_network_split = split
        self.sync_all()

    def check_block_fund_addresses(self, block_hash, expected_addresses):
        block_data = self.nodes[0].getblock(block_hash, 1)
        tx_data = block_data["tx"]
        for tx in tx_data:
            vout_data = self.nodes[0].getrawtransaction(tx, 1)["vout"]

            # We expect the coinbase transaction to have one output for each fund address
            # (foundation one is always mandatory, secure node and super node ones only starting from fork 3)
            # and one output for the miner reward.
            assert_equal(len(vout_data), len(expected_addresses) + 1)

            for i in range(1, len(vout_data)):
                vout = vout_data[i]
                assert_equal(vout["scriptPubKey"]["addresses"][0], expected_addresses[i - 1])

    def run_test(self):

        '''
        This test checks that the right addresses are used for community, securenode and supernode funds
        when mining blocks.
        '''

        # network topology: (0)

        mark_logs("Node 0 generates 500 blocks", self.nodes, DEBUG_MODE)
        blocks = self.nodes[0].generate(500)
        self.sync_all()

        # Consider that blocks[0] is not the genesis block but the block at height 1 (second block)
        # For this reason, all fork heights must be decreased by 1

        current_fork_height = 1 - 1 # chainsplit fork
        mark_logs("Check chainsplit fork address [height = {}]".format(current_fork_height + 1), self.nodes, DEBUG_MODE)
        self.check_block_fund_addresses(blocks[current_fork_height], ["zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD"])
        self.check_block_fund_addresses(blocks[current_fork_height + 1], ["zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD"])

        current_fork_height = 101 - 1 # community fund fork
        mark_logs("Check community fund fork address [height = {}]".format(current_fork_height + 1), self.nodes, DEBUG_MODE)
        self.check_block_fund_addresses(blocks[current_fork_height - 1], ["zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD"])
        self.check_block_fund_addresses(blocks[current_fork_height], ["zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD"])
        self.check_block_fund_addresses(blocks[current_fork_height + 1], ["zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD"])

        current_fork_height = 105 - 1 # null transaction fork
        mark_logs("Check null transaction fork addresses [height = {}]".format(current_fork_height + 1), self.nodes, DEBUG_MODE)
        self.check_block_fund_addresses(blocks[current_fork_height - 1], ["zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD"])
        self.check_block_fund_addresses(blocks[current_fork_height], ["zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD", "zrQG6x9j33DLbCfzAqu3qKMe7z1VDL1z2L7", "zrMasbhB1yyfQ5RBUm7NPcEjGWZdRneWCEx"])
        self.check_block_fund_addresses(blocks[current_fork_height + 1], ["zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD", "zrQG6x9j33DLbCfzAqu3qKMe7z1VDL1z2L7", "zrMasbhB1yyfQ5RBUm7NPcEjGWZdRneWCEx"])

        current_fork_height = ForkHeights['SC_VERSION'] - 1 # sidechain version fork
        mark_logs("Check sidechain version fork addresses [height = {}]".format(current_fork_height + 1), self.nodes, DEBUG_MODE)
        self.check_block_fund_addresses(blocks[current_fork_height - 1], ["zrQWJd1fhtkQtrjbYPXfHFF1c61DUtiXcCD", "zrQG6x9j33DLbCfzAqu3qKMe7z1VDL1z2L7", "zrMasbhB1yyfQ5RBUm7NPcEjGWZdRneWCEx"])
        self.check_block_fund_addresses(blocks[current_fork_height], ["zrLaR63UYCHVvo5BJHoMUTuZFPmcUu866wB", "zrPaU1KWpNrg5fcLsSk17z7cc71FvnVnXxi", "zrMna8FbuTyrvFikAsmQMyAfufF3WoGksFu"])
        self.check_block_fund_addresses(blocks[current_fork_height + 1], ["zrLaR63UYCHVvo5BJHoMUTuZFPmcUu866wB", "zrPaU1KWpNrg5fcLsSk17z7cc71FvnVnXxi", "zrMna8FbuTyrvFikAsmQMyAfufF3WoGksFu"])

if __name__ == '__main__':
    FundAddressesTest().main()
