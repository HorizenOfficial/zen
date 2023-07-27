#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import ForkHeights, BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, start_nodes, mark_logs, connect_nodes_bi
from decimal import Decimal

NUMB_OF_NODES = 1
#NUMB_OF_NODES = 3
DEBUG_MODE = 1

class FundAddressesTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir)

        if NUMB_OF_NODES > 1:
            for i in range(NUMB_OF_NODES - 1):
                connect_nodes_bi(self.nodes, i, i + 1)

        self.sync_all()

    def generate_multisig_address(self):
        """
        Generate a new multisignature address (1:1) to be used in REGTEST.

        IMPORTANT NOTE: it requires at least 3 nodes to perform sanity checks.
        """

        # Generate a "random" number of blocks
        self.nodes[0].generate(1000)
        self.sync_all()

        # Generate a "random" address
        for _ in range(7):
            addr = self.nodes[1].getnewaddress()

        addr_obj = self.nodes[1].validateaddress(addr)

        # Generate a multisignature address (1:1)
        multisig_addr = self.nodes[1].addmultisigaddress(1, [addr_obj['pubkey']])


        #########################
        # TEST MULTISIG ADDRESS #
        #########################
        zen_value = Decimal('10.00000000')
        tx_id = self.nodes[0].sendtoaddress(multisig_addr, zen_value)
        self.nodes[0].generate(1)
        self.sync_all()

        tx_details = self.nodes[0].gettransaction(tx_id, True)
        raw_tx = self.nodes[0].decoderawtransaction(tx_details['hex'])

        vout = False

        for outpoint in raw_tx['vout']:
            if outpoint['value'] == zen_value:
                vout = outpoint
                break

        assert vout

        inputs = [{ "txid" : tx_id, "vout" : vout['n'], "scriptPubKey" : vout['scriptPubKey']['hex']}]
        outputs = { self.nodes[2].getnewaddress(): 10.0 }
        rawTx = self.nodes[1].createrawtransaction(inputs, outputs)
        rawTxSigned = self.nodes[1].signrawtransaction(rawTx, inputs)
        assert_equal(rawTxSigned['complete'], True)
        self.nodes[1].sendrawtransaction(rawTxSigned['hex'])
        self.sync_all()

        self.nodes[0].generate(1)
        self.sync_all()

        assert_equal(self.nodes[2].getbalance("", 0), 10)

        print(f"Generated new multisig address [REGTEST]: {multisig_addr}")

        quit()

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

        # To generate a new multisignature address, uncomment the next line and set NUMB_OF_NODES to 3
        #self.generate_multisig_address()

        mark_logs("Node 0 generates 1000 blocks", self.nodes, DEBUG_MODE)
        blocks = self.nodes[0].generate(1000)
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

        current_fork_height = ForkHeights['SHIELDED_POOL_DEPRECATION'] - 1 # shielded pool deprecation fork
        mark_logs("Check shielded pool deprecation fork addresses [height = {}]".format(current_fork_height + 1), self.nodes, DEBUG_MODE)
        self.check_block_fund_addresses(blocks[current_fork_height - 1], ["zrLaR63UYCHVvo5BJHoMUTuZFPmcUu866wB", "zrPaU1KWpNrg5fcLsSk17z7cc71FvnVnXxi", "zrMna8FbuTyrvFikAsmQMyAfufF3WoGksFu"])
        self.check_block_fund_addresses(blocks[current_fork_height], ["zrBsetyTneFLjJzgnS3YTs6od689MNRyyJ7", "zrACbdqaYnprPbPkuf5P2ZDTfes3dQoJCvz", "zrPTHLGBvs4j4Fd85aXVhqUGrPsNsWGqkab"])
        self.check_block_fund_addresses(blocks[current_fork_height + 1], ["zrBsetyTneFLjJzgnS3YTs6od689MNRyyJ7", "zrACbdqaYnprPbPkuf5P2ZDTfes3dQoJCvz", "zrPTHLGBvs4j4Fd85aXVhqUGrPsNsWGqkab"])

if __name__ == '__main__':
    FundAddressesTest().main()
