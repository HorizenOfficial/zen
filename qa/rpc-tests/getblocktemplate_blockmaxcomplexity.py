#!/usr/bin/env python3
# Copyright (c) 2018 The Zen developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_node, connect_nodes, sync_mempools

from decimal import Decimal

# Test -blockmaxcomplexity
class GetBlockTemplateBlockMaxComplexityTest(BitcoinTestFramework):

    def setup_network(self):
    # Create 6 nodes with different -blockmaxcomplexity cases
        args0 = ["-blockmaxcomplexity=1"]
        args1 = ["-blockmaxcomplexity=13"]
        args2 = ["-blockmaxcomplexity=204"]
        args3 = ["-blockmaxcomplexity=313"]
        args4 = ["-blockmaxcomplexity=0"] # 0 value equals to infinity max block compexity
        args5 = ["-blockmaxcomplexity=-1"] # negative value equals to infinity max block compexity
        args6 = [] # no parameter -> equals to infinity max block compexity
        args7 = ["-blockmaxcomplexity=204", "-deprecatedgetblocktemplate"] # disables block complexity checks
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, args0))
        self.nodes.append(start_node(1, self.options.tmpdir, args1))
        self.nodes.append(start_node(2, self.options.tmpdir, args2))
        self.nodes.append(start_node(3, self.options.tmpdir, args3))
        self.nodes.append(start_node(4, self.options.tmpdir, args4))
        self.nodes.append(start_node(5, self.options.tmpdir, args5))
        self.nodes.append(start_node(6, self.options.tmpdir, args6))
        self.nodes.append(start_node(7, self.options.tmpdir, args7))
        connect_nodes(self.nodes[1], 0)
        connect_nodes(self.nodes[2], 0)
        connect_nodes(self.nodes[3], 0)
        connect_nodes(self.nodes[4], 0)
        connect_nodes(self.nodes[5], 0)
        connect_nodes(self.nodes[6], 0)
        connect_nodes(self.nodes[7], 0)
        self.is_network_split = False
        self.sync_all

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 8)

    def run_test(self):
        self.nodes[0].generate(100)
        self.sync_all()
        # Mine 50 blocks. After this, nodes[0] blocks
        # 1 to 50 are spend-able.
        self.nodes[1].generate(50)
        self.sync_all()

        node1_taddr = self.nodes[1].getnewaddress()

        # Create transaction 3 transactions with 10 inputs each
        # Each transaction complexity will be equal to 10*10=100
        for n in range(0,3):
            tx_inputs = []
            tx_inputs_sum = Decimal('0.0')
            for i in range(0,10):
                tx_inputs.append({"txid" : self.nodes[0].listunspent()[i]['txid'], 'vout' : 0})
                tx_inputs_sum += self.nodes[0].listunspent()[i]['amount']
            tx_outputs = {self.nodes[1].getnewaddress() : tx_inputs_sum - Decimal('0.0001')}

            tx_rawtx = self.nodes[0].createrawtransaction(tx_inputs, tx_outputs)
            tx_rawtx = self.nodes[0].signrawtransaction(tx_rawtx)
            tx_rawtx = self.nodes[0].sendrawtransaction(tx_rawtx['hex'])
            # Wait for wallet to catch up with mempool for listunspent call
            sync_mempools([self.nodes[0]])

        # Create transaction 3 transactions with 2 inputs each
        # Each transaction complexity will be equal to 2*2=4
        for n in range(0,3):
            tx_inputs = []
            tx_inputs_sum = Decimal('0.0')
            for i in range(0,2):
                tx_inputs.append({"txid" : self.nodes[0].listunspent()[i]['txid'], 'vout' : 0})
                tx_inputs_sum += self.nodes[0].listunspent()[i]['amount']
            tx_outputs = {self.nodes[1].getnewaddress() : tx_inputs_sum - Decimal('0.0001')}

            tx_rawtx = self.nodes[0].createrawtransaction(tx_inputs, tx_outputs)
            tx_rawtx = self.nodes[0].signrawtransaction(tx_rawtx)
            tx_rawtx = self.nodes[0].sendrawtransaction(tx_rawtx['hex'])
            # Wait for wallet to catch up with mempool for listunspent call
            sync_mempools([self.nodes[0]])

        self.sync_all()

        # At this moment mempool contains 3 UTXO with 10 inputs and 3 UTXO with 2 inputs
        # Check blockmaxcomplexity on each node:
        # Test 1: nodes[0] has blockmaxcompexity=1
        tmpl = self.nodes[0].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 0)

        # Test 2: nodes[1] has blockmaxcompexity=13
        tmpl = self.nodes[1].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 3) # 3 Tx with 2 inputs each

        # Test 3: nodes[2] has blockmaxcompexity=204
        tmpl = self.nodes[2].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 2) # 2 TX with 10 inputs each
    
        # Test 4: nodes[3] has blockmaxcompexity=313
        tmpl = self.nodes[3].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 6)

        # Test 5: nodes[4] has blockmaxcompexity=0
        tmpl = self.nodes[4].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 6)

        # Test 6: nodes[5] has blockmaxcompexity=-1
        tmpl = self.nodes[5].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 6)

        # Test 7: nodes[6] has no blockmaxcompexity parameter
        tmpl = self.nodes[6].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 6)

        # Test 8: nodes[7] has blockmaxcompexity=204 and set "-deprecatedgetblocktemplate"
        tmpl = self.nodes[7].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 6) # blockmaxcompexity was disabled by -deprecatedgetblocktemplate


if __name__ == '__main__':
    GetBlockTemplateBlockMaxComplexityTest().main()
