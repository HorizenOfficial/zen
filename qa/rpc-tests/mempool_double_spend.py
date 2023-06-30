#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test the double spending of several nested transactions.
#

from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, connect_nodes, \
    sync_blocks, gather_inputs, start_nodes, sync_mempools, \
    connect_nodes_bi, initialize_chain_clean, mark_logs, \
    disconnect_nodes
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 3
DOUBLE_SPEND_NODE_INDEX = NUMB_OF_NODES - 1    # The node that tries to perform the double spend
SC_COINS_MAT = 1

class TxnMallTest(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args= [['-blockprioritysize=0',
            '-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib',
            '-scproofqueuesize=0', '-logtimemicros=1', '-sccoinsmaturity=%d' % SC_COINS_MAT]] * NUMB_OF_NODES )

        if not split:
            self.join_network()
        else:
            self.split_network()
        
        self.sync_all()

    def join_network(self):
        for idx in range(NUMB_OF_NODES - 1):
            connect_nodes_bi(self.nodes, idx, idx+1)
        self.is_network_split = False

    def run_test(self):

        node0_address = self.nodes[0].getnewaddress()
        iterations = 100

        mark_logs("Node 0 mines 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        mark_logs("Node 0 mines 100 blocks to make the coinbase of the first block spendable", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(100)

        mark_logs("Node 0 balance: " + str(self.nodes[0].getbalance()), self.nodes, DEBUG_MODE)
        mark_logs("Node {} balance: {}".format(DOUBLE_SPEND_NODE_INDEX, str(self.nodes[DOUBLE_SPEND_NODE_INDEX].getbalance())), self.nodes, DEBUG_MODE)

        mark_logs("Blockchain height: " + str(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)

        spendable_amount = Decimal("10.0")
        fee_amount = Decimal("0.0001")
        total_amount = spendable_amount + fee_amount * iterations
        mark_logs("Node 0 sends {} zen to a known address".format(total_amount), self.nodes, DEBUG_MODE)

        mark_logs("Initial address balance: " + str(self.nodes[0].z_getbalance(node0_address)), self.nodes, DEBUG_MODE)
        self.nodes[0].sendfrom("", node0_address, total_amount)
        self.nodes[0].generate(1)
        mark_logs("Current address balance: " + str(self.nodes[0].z_getbalance(node0_address)), self.nodes, DEBUG_MODE)

        self.sync_all()

        mark_logs("Split the network", self.nodes, DEBUG_MODE)
        self.split_network(DOUBLE_SPEND_NODE_INDEX - 1)

        # Search the unspent utxo containing 10.1 coins.
        list_unspent = self.nodes[0].listunspent()
        for unspent in list_unspent:
            current_utxo = unspent
            if current_utxo["amount"] == total_amount:
                break

        # Assert that the utxo has been found.
        assert_equal(current_utxo["amount"], total_amount)

        change_amount = current_utxo["amount"]
        txid = current_utxo["txid"]
        vout = current_utxo["vout"]

        # Create a special Tx that moves all the 10.1 coins of the utxo
        recipient = self.nodes[DOUBLE_SPEND_NODE_INDEX].getnewaddress()
        inputs = [{"txid": current_utxo["txid"], "vout": current_utxo["vout"]}]
        outputs = {}
        outputs[recipient] = total_amount - fee_amount
        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
        special_tx = self.nodes[0].signrawtransaction(rawtx)

        mark_logs("Node 0 adds to mempool several transactions recursively spending the starting utxo", self.nodes, DEBUG_MODE)
        for _ in range(iterations):
            recipient = self.nodes[DOUBLE_SPEND_NODE_INDEX].getnewaddress()
            sent_amount = spendable_amount / iterations
            change_amount = change_amount - sent_amount - fee_amount
            inputs = []
            inputs.append({"txid": txid, "vout": vout})
            outputs = {}
            outputs[node0_address] = change_amount
            outputs[recipient] = sent_amount
            try:
                rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
                signedRawTx = self.nodes[0].signrawtransaction(rawtx)
                senttxid = self.nodes[0].sendrawtransaction(signedRawTx['hex'])
                senttx = self.nodes[0].getrawtransaction(senttxid, 1)
            except JSONRPCException as e:
                print(e.error['message'])
                assert(False)

            # Find the index number of the change output
            for out in senttx["vout"]:
                if out["scriptPubKey"]["addresses"] == [node0_address]:
                    txid = senttxid
                    vout = out["n"]
                    break

        # Sync mempools except for the last (disconnected) node
        mark_logs("Waiting for honest nodes to sync mempool", self.nodes, DEBUG_MODE)
        sync_mempools(self.nodes[0 : NUMB_OF_NODES - 1])

        mark_logs("Check that all the nodes have {} transactions in the mempool except the last one".format(iterations), self.nodes, DEBUG_MODE)
        for i in range(NUMB_OF_NODES):
            mark_logs("Node {} mempool size: {}".format(i, len(self.nodes[i].getrawmempool())), self.nodes, DEBUG_MODE)
            if i != DOUBLE_SPEND_NODE_INDEX:
                assert_equal(len(self.nodes[i].getrawmempool()), iterations)
            else:
                assert_equal(len(self.nodes[i].getrawmempool()), 0)

        mark_logs("Node {} sends a transaction spending all the coins of the initial utxo ({} coins)".format(DOUBLE_SPEND_NODE_INDEX, total_amount), self.nodes, DEBUG_MODE)
        try:
            senttxid = self.nodes[DOUBLE_SPEND_NODE_INDEX].sendrawtransaction(special_tx['hex'])
            senttx = self.nodes[DOUBLE_SPEND_NODE_INDEX].getrawtransaction(senttxid, 1)
        except JSONRPCException as e:
            print(e.error['message'])
            assert(False)

        mark_logs("Check that the new transaction has been added only to the mempool of the node {}".format(DOUBLE_SPEND_NODE_INDEX), self.nodes, DEBUG_MODE)
        for i in range(NUMB_OF_NODES):
            mark_logs("Node {} mempool size: {}".format(i, len(self.nodes[i].getrawmempool())), self.nodes, DEBUG_MODE)
            if i != DOUBLE_SPEND_NODE_INDEX:
                assert_equal(len(self.nodes[i].getrawmempool()), iterations)
            else:
                assert_equal(len(self.nodes[i].getrawmempool()), 1)

        mark_logs("Node {} mines a block containing this transaction".format(DOUBLE_SPEND_NODE_INDEX), self.nodes, DEBUG_MODE)
        self.nodes[DOUBLE_SPEND_NODE_INDEX].generate(1)

        mark_logs(("Join the network and sync; all the nodes should remove the pending transactions from mempool"
                   "(conflicting with the transaction that has just been mined)"), self.nodes, DEBUG_MODE)
        self.join_network()
        self.sync_all()

        mark_logs("Check that the mempool of every node is empty", self.nodes, DEBUG_MODE)
        for i in range(NUMB_OF_NODES):
            mark_logs("Node {} mempool size: {}".format(i, len(self.nodes[i].getrawmempool())), self.nodes, DEBUG_MODE)
            assert_equal(len(self.nodes[i].getrawmempool()), 0)

        mark_logs("Current balance of the sending address: {:.8f}".format(self.nodes[0].z_getbalance(node0_address)), self.nodes, DEBUG_MODE)

        # Check the consistency of all "getbalance" RPC commands
        for i in range(NUMB_OF_NODES):
            node_amount = self.nodes[i].getbalance()
            assert_equal(node_amount, self.nodes[i].getbalance(""))
            assert_equal(node_amount, self.nodes[i].getbalance("*"))
            assert_equal(node_amount, Decimal(self.nodes[i].z_gettotalbalance()["total"]))

if __name__ == '__main__':
    TxnMallTest().main()
