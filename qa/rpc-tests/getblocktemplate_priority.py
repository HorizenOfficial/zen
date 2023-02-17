#!/usr/bin/env python3
# Copyright (c) 2018 The Zen developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_node, connect_nodes

from decimal import Decimal
import time

def sort_by_confirmations(json):
    try:
        return int(json['confirmations'])
    except KeyError:
        return 0

class GetBlockTemplatePriorityTest(BitcoinTestFramework):

    def setup_network(self):
        args0 = ["-printpriority"]
        args1 = ["-printpriority", "-blockmaxcomplexity=9"]
        args2 = ["-printpriority", "-blockprioritysize=0"]
        args3 = ["-printpriority", "-blockprioritysize=0", "-blockmaxcomplexity=9"]

        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, args0))
        self.nodes.append(start_node(1, self.options.tmpdir, args1))
        self.nodes.append(start_node(2, self.options.tmpdir, args2))
        self.nodes.append(start_node(3, self.options.tmpdir, args3))

        connect_nodes(self.nodes[1], 0)
        connect_nodes(self.nodes[2], 0)
        connect_nodes(self.nodes[3], 0)

        self.is_network_split = False
        self.sync_all

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4)

    def calculate_fee_rate(self, fee, rawtx):
        size = len (rawtx['hex'])

        if size == 0:
            return 0

        # add a multiplier for improving precision. We are interested in having
        # proportional rates, the actual rate on c++ core is different but the
        # order will be the same
        MULTIP_CONST = 1000000
        return Decimal( fee*MULTIP_CONST/size )


    def run_test(self):
        self.nodes[0].generate(100)
        self.sync_all()
        # Mine 50 blocks. After this, nodes[0] blocks
        # 1 to 50 are spend-able.
        self.nodes[1].generate(50)
        self.sync_all()

        # sort node[0] unspent transactions from oldest to newest
        node0_unspent = self.nodes[0].listunspent()
        node0_unspent.sort(key=sort_by_confirmations, reverse=True)

        # Generate 4 addresses for node[0]
        node0_taddrs = []
        node0_txs = []
        node0_txs_amount = []
        node0_txs_fee_rate = []

        for i in range(0,4):
            node0_taddrs.append(self.nodes[0].getnewaddress())

        # Create Tx with 2 inputs with equal amount.
        # Tx complexity equal to 2*2=4
        tx1_inputs = []
        tx1_inputs_sum = Decimal('0.0')
        tx1_fee_rate   = Decimal('0.0')

        tx1_inputs.append({"txid" : node0_unspent[0]['txid'], 'vout' : 0})
        tx1_inputs_sum += node0_unspent[0]['amount']
        tx1_inputs.append({"txid" : node0_unspent[3]['txid'], 'vout' : 0})
        tx1_inputs_sum += node0_unspent[3]['amount']

        tx1_fee = Decimal('0.0001')

        tx1_outputs = {node0_taddrs[0] : tx1_inputs_sum - tx1_fee}

        tx1_rawtx = self.nodes[0].createrawtransaction(tx1_inputs, tx1_outputs)
        tx1_rawtx = self.nodes[0].signrawtransaction(tx1_rawtx)
            
        node0_txs.append(self.nodes[0].sendrawtransaction(tx1_rawtx['hex']))
        node0_txs_amount.append(tx1_inputs_sum - tx1_fee)
        node0_txs_fee_rate.append( self.calculate_fee_rate( tx1_fee, tx1_rawtx) )

        # Create another Tx with 2 inputs with the same amount and total input confirmations as first Tx
        # Tx complexity equal to 2*2=4
        # Tx2 will have prirority equal to Tx1, but bigger feeRate.
        tx2_inputs = []
        tx2_inputs_sum = Decimal('0.0')

        tx2_inputs.append({"txid" : node0_unspent[1]['txid'], 'vout' : 0})
        tx2_inputs_sum += node0_unspent[1]['amount']
        tx2_inputs.append({"txid" : node0_unspent[2]['txid'], 'vout' : 0})
        tx2_inputs_sum += node0_unspent[2]['amount']

        tx2_fee = Decimal('0.0005')

        tx2_outputs = {node0_taddrs[1] : tx2_inputs_sum - tx2_fee}

        tx2_rawtx = self.nodes[0].createrawtransaction(tx2_inputs, tx2_outputs)
        tx2_rawtx = self.nodes[0].signrawtransaction(tx2_rawtx)
            
        node0_txs.append(self.nodes[0].sendrawtransaction(tx2_rawtx['hex']))
        node0_txs_amount.append(tx2_inputs_sum - tx2_fee)
        node0_txs_fee_rate.append( self.calculate_fee_rate( tx2_fee, tx2_rawtx) )

        # Create 2 Txs with 2 inputs each.
        for n in range(2,4):
            tx_inputs = []
            tx_inputs_sum = Decimal('0.0')
            for i in range(0,2):
                tx_inputs.append({"txid" : node0_unspent[(2*n)+i]['txid'], 'vout' : 0})
                tx_inputs_sum += node0_unspent[(2*n)+i]['amount']

            tx_fee = Decimal('0.0001')
            if n == 3:
                tx_fee = Decimal('0.0003')
            tx_outputs = {node0_taddrs[n] : tx_inputs_sum - tx_fee}

            tx_rawtx = self.nodes[0].createrawtransaction(tx_inputs, tx_outputs)
            tx_rawtx = self.nodes[0].signrawtransaction(tx_rawtx)

            node0_txs.append(self.nodes[0].sendrawtransaction(tx_rawtx['hex']))
            node0_txs_amount.append(tx_inputs_sum - tx_fee)
            node0_txs_fee_rate.append( self.calculate_fee_rate( tx_fee, tx_rawtx))

        self.sync_all()
        time.sleep(5)

#        for i in range(0, len(node0_txs)):
#            print "UTXO[%d]: sum=%f, feeRate=%f, txid[%s]" % (i, node0_txs_amount[i], node0_txs_fee_rate[i], node0_txs[i]) 

        # At this moment mempool contains 4 UTXO with 2 inputs each, with the same coins amount
        # UTXO by priorities has order: node0_txs[1], node0_txs[0], node0_txs[2], node0_txs[3]
        # UTXO by feeRates has order: node0_txs[1], node0_txs[3], node0_txs[0], node0_txs[2]
        # Note: node0_txs[0] and node1_tx[1] has equal priorities but different feeRates.

        # Test 1: nodes[0] must have all 4 txs in order by priority, then by feeRate
        tmpl = self.nodes[0].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 4)
        assert_equal(tmpl['transactions'][0]['hash'], node0_txs[1])
        assert_equal(tmpl['transactions'][1]['hash'], node0_txs[0])
        assert_equal(tmpl['transactions'][2]['hash'], node0_txs[2])
        assert_equal(tmpl['transactions'][3]['hash'], node0_txs[3])

        # Test 2: nodes[1] has blockmaxcompexity=9, must contain 2 txs in order by priority, then by fee
        tmpl = self.nodes[1].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 2) # 2 Tx with 2 inputs each
        assert_equal(tmpl['transactions'][0]['hash'], node0_txs[1])
        assert_equal(tmpl['transactions'][1]['hash'], node0_txs[0])

        # Test 3: nodes[2] must have all 4 txs in order by feeRate, then by priority
        tmpl = self.nodes[2].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 4)
        assert_equal(tmpl['transactions'][0]['hash'], node0_txs[1])
        assert_equal(tmpl['transactions'][1]['hash'], node0_txs[3])

        # check fee rates for tx with equal fee
        # ---------
        # NOTE: In the c++ core, feeRate is related to (fee / tx serialization size).
        # Tx serialization contains signature serialization, and signature length can vary sligthly (1 or 2 bytes
        # depending on the value of R and S points on the ECDSA curve).
        # Therefore we might have a feeRate that randomly slightly varies across different runs for a fixed fee.
        if node0_txs_fee_rate[0] >= node0_txs_fee_rate[2]: 
            assert_equal(tmpl['transactions'][2]['hash'], node0_txs[0])
            assert_equal(tmpl['transactions'][3]['hash'], node0_txs[2])
        else:
            assert_equal(tmpl['transactions'][2]['hash'], node0_txs[2])
            assert_equal(tmpl['transactions'][3]['hash'], node0_txs[0])

        # Test 4: nodes[3] has blockmaxcompexity=9, must contain 2 txs in order by fee, then by priority
        tmpl = self.nodes[3].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 2) # 2 Tx with 2 inputs each
        assert_equal(tmpl['transactions'][0]['hash'], node0_txs[1])
        assert_equal(tmpl['transactions'][1]['hash'], node0_txs[3])


        # Add orphan transaction to mempool. Fee = 0.0009, 4 inputs (1 from mempool)
        node0_orphan_tx = self.nodes[0].getnewaddress()
        node0_taddrs.append(node0_orphan_tx)
        orphan_inputs = []
        orphan_inputs_sum = Decimal('0.0')
        orphan_inputs.append({'txid': node0_txs[1], 'vout' : 0})
        orphan_inputs_sum += node0_txs_amount[1]
        for i in range(0,3):
            orphan_inputs.append({"txid" : self.nodes[0].listunspent()[i]['txid'], 'vout' : 0})
            orphan_inputs_sum += self.nodes[0].listunspent()[i]['amount']


        orphan_fee = Decimal('0.0009')
        orphan_outputs = {node0_orphan_tx : orphan_inputs_sum - orphan_fee}

        orphantx_rawtx = self.nodes[0].createrawtransaction(orphan_inputs, orphan_outputs)
        orphantx_rawtx = self.nodes[0].signrawtransaction(orphantx_rawtx)
        node0_txs.append(self.nodes[0].sendrawtransaction(orphantx_rawtx['hex']))
        node0_txs_amount.append(orphan_inputs_sum - orphan_fee)
        node0_txs_fee_rate.append( self.calculate_fee_rate( orphan_fee, orphantx_rawtx) )

        self.sync_all()
        time.sleep(5) # wait to be sured, tah orphan tx was added to the wallet and mempool

#        for i in range(0, len(node0_txs)):
#            print "UTXO[%d]: sum=%f, feeRate=%f, txid[%s]" % (i, node0_txs_amount[i], node0_txs_fee_rate[i], node0_txs[i]) 

        # At this moment mempool contains 4 UTXO (with 2 inputs each, with the same coins amount)
        # and 1 orphan UTXO (node0_txs[4]) with 4 inputs (Tx complexity = 16), that depends on node0_txs[1]
        # UTXO by priorities has order: node0_txs[1], node0_txs[4], node0_txs[0], node0_txs[2], node0_txs[3]
        # UTXO by fees has order: node0_txs[1], node0_txs[4], node0_txs[3], node0_txs[0], node0_txs[2]
        # Note: node0_txs[0] and node1_tx[1] has equal priorities but different fees.

        # Test 5: nodes[0] must have all 5 txs in order by priority, then by fee
        tmpl = self.nodes[0].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 5)
        assert_equal(tmpl['transactions'][0]['hash'], node0_txs[1])
        assert_equal(tmpl['transactions'][1]['hash'], node0_txs[4])
        assert_equal(tmpl['transactions'][2]['hash'], node0_txs[0])
        assert_equal(tmpl['transactions'][3]['hash'], node0_txs[2])
        assert_equal(tmpl['transactions'][4]['hash'], node0_txs[3])


        # Test 6: nodes[1] has blockmaxcompexity=9, must contain 2 txs in order by priority, then by fee
        # Note: node0_txs[4] has complexity greater than max value -> should be skipped
        tmpl = self.nodes[1].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 2) # 2 Tx with 2 inputs each
        assert_equal(tmpl['transactions'][0]['hash'], node0_txs[1])
        assert_equal(tmpl['transactions'][1]['hash'], node0_txs[0])

        # Test 7: nodes[2] must have all 5 txs in order by fee, then by priority
        tmpl = self.nodes[2].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 5)
        assert_equal(tmpl['transactions'][0]['hash'], node0_txs[1])
        assert_equal(tmpl['transactions'][1]['hash'], node0_txs[4])
        assert_equal(tmpl['transactions'][2]['hash'], node0_txs[3])

        # Test 8: nodes[3] has blockmaxcompexity=9, must contain 2 txs in order by fee, then by priority
        # Note: node0_txs[4] has complexity greater than max value -> should be skipped
        tmpl = self.nodes[3].getblocktemplate()
        assert_equal(len(tmpl['transactions']), 2) # 2 Tx with 2 inputs each
        assert_equal(tmpl['transactions'][0]['hash'], node0_txs[1])
        assert_equal(tmpl['transactions'][1]['hash'], node0_txs[3])


if __name__ == '__main__':
    GetBlockTemplatePriorityTest().main()
