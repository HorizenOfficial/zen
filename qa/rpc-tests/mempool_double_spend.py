#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test the double spending of several nested transactions.
#

from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, start_nodes, sync_mempools, \
    connect_nodes_bi, initialize_chain_clean, mark_logs, disconnect_nodes
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 3
SC_COINS_MAT = 1

DOUBLE_SPEND_NODE_INDEX = NUMB_OF_NODES - 1     # The node that tries to perform the double spend
HONEST_NODES = list(range(NUMB_OF_NODES))
HONEST_NODES.remove(DOUBLE_SPEND_NODE_INDEX)
MAIN_NODE = HONEST_NODES[0]                     # Hardcoded alias, do not change


class TxnMallTest(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        assert_equal(MAIN_NODE, HONEST_NODES[0])

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args= [['-blockprioritysize=0',
            '-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib',
            '-scproofqueuesize=0', '-logtimemicros=1', '-sccoinsmaturity=%d' % SC_COINS_MAT]] * NUMB_OF_NODES )

        for idx in range(NUMB_OF_NODES - 1):
            connect_nodes_bi(self.nodes, idx, idx + 1)
        self.is_network_split = split
        self.sync_all()

    def join_network(self):
        #Join the (previously split) network pieces together
        idx = DOUBLE_SPEND_NODE_INDEX
        if idx == NUMB_OF_NODES - 1:
            connect_nodes_bi(self.nodes, idx, idx - 1)
        elif idx == 0:
            connect_nodes_bi(self.nodes, idx, idx + 1)
        else:
            disconnect_nodes(self.nodes[idx - 1], idx + 1)
            disconnect_nodes(self.nodes[idx + 1], idx - 1)
            connect_nodes_bi(self.nodes, idx, idx - 1)
            connect_nodes_bi(self.nodes, idx, idx + 1)
        self.is_network_split = False

    def split_network(self):
        # Disconnect the "double spend" node from the network
        idx = DOUBLE_SPEND_NODE_INDEX
        if idx == NUMB_OF_NODES - 1:
            disconnect_nodes(self.nodes[idx], idx - 1)
            disconnect_nodes(self.nodes[idx - 1], idx)
        elif idx == 0:
            disconnect_nodes(self.nodes[idx], idx + 1)
            disconnect_nodes(self.nodes[idx + 1], idx)
        else:
            disconnect_nodes(self.nodes[idx], idx - 1)
            disconnect_nodes(self.nodes[idx - 1], idx)
            disconnect_nodes(self.nodes[idx], idx + 1)
            disconnect_nodes(self.nodes[idx + 1], idx)
            connect_nodes_bi(self.nodes, idx - 1, idx + 1)
        self.is_network_split = True

    def run_test(self):

        node0_address = self.nodes[MAIN_NODE].getnewaddress()
        iterations = 100

        mark_logs(f"Node {MAIN_NODE} mines 1 block", self.nodes, DEBUG_MODE)
        self.nodes[MAIN_NODE].generate(1)
        mark_logs(f"Node {MAIN_NODE} mines 100 blocks to make the coinbase of the first block spendable", self.nodes, DEBUG_MODE)
        self.nodes[MAIN_NODE].generate(100)

        mark_logs(f"Node {MAIN_NODE} balance: {self.nodes[MAIN_NODE].getbalance()}", self.nodes, DEBUG_MODE)
        mark_logs(f"Node {DOUBLE_SPEND_NODE_INDEX} balance: {self.nodes[DOUBLE_SPEND_NODE_INDEX].getbalance()}", self.nodes, DEBUG_MODE)

        mark_logs(f"Blockchain height: {self.nodes[MAIN_NODE].getblockcount()}", self.nodes, DEBUG_MODE)

        spendable_amount = Decimal("10.0")
        fee_amount = Decimal("0.0001")
        total_amount = spendable_amount + fee_amount * iterations
        mark_logs(f"Node {MAIN_NODE} sends {total_amount} zen to a known address", self.nodes, DEBUG_MODE)

        mark_logs(f"Initial address balance: {self.nodes[MAIN_NODE].z_getbalance(node0_address)}", self.nodes, DEBUG_MODE)
        self.nodes[MAIN_NODE].sendfrom("", node0_address, total_amount)
        self.nodes[MAIN_NODE].generate(1)
        mark_logs(f"Current address balance: {self.nodes[MAIN_NODE].z_getbalance(node0_address)}", self.nodes, DEBUG_MODE)

        self.sync_all()

        mark_logs("Split the network", self.nodes, DEBUG_MODE)
        self.split_network()

        # Search the unspent utxo containing 10.1 coins.
        list_unspent = self.nodes[MAIN_NODE].listunspent()
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
        rawtx = self.nodes[MAIN_NODE].createrawtransaction(inputs, outputs)
        special_tx = self.nodes[MAIN_NODE].signrawtransaction(rawtx)

        mark_logs(f"Node {MAIN_NODE} adds to mempool several transactions recursively spending the starting utxo", self.nodes, DEBUG_MODE)
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
                rawtx = self.nodes[MAIN_NODE].createrawtransaction(inputs, outputs)
                signedRawTx = self.nodes[MAIN_NODE].signrawtransaction(rawtx)
                senttxid = self.nodes[MAIN_NODE].sendrawtransaction(signedRawTx['hex'])
                senttx = self.nodes[MAIN_NODE].getrawtransaction(senttxid, 1)
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
        sync_mempools([self.nodes[i] for i in HONEST_NODES])

        mark_logs(f"Check that all the nodes have {iterations} transactions in the mempool except the last one", self.nodes, DEBUG_MODE)
        for i in range(NUMB_OF_NODES):
            mark_logs(f"Node {i} mempool size: {len(self.nodes[i].getrawmempool())}", self.nodes, DEBUG_MODE)
            if i != DOUBLE_SPEND_NODE_INDEX:
                assert_equal(len(self.nodes[i].getrawmempool()), iterations)
            else:
                assert_equal(len(self.nodes[i].getrawmempool()), 0)

        mark_logs(f"Node {DOUBLE_SPEND_NODE_INDEX} sends a transaction spending all the coins of the initial utxo ({total_amount} coins)", self.nodes, DEBUG_MODE)
        try:
            senttxid = self.nodes[DOUBLE_SPEND_NODE_INDEX].sendrawtransaction(special_tx['hex'])
            senttx = self.nodes[DOUBLE_SPEND_NODE_INDEX].getrawtransaction(senttxid, 1)
        except JSONRPCException as e:
            print(e.error['message'])
            assert(False)

        mark_logs(f"Check that the new transaction has been added only to the mempool of the node {DOUBLE_SPEND_NODE_INDEX}", self.nodes, DEBUG_MODE)
        for i in range(NUMB_OF_NODES):
            mark_logs(f"Node {i} mempool size: {len(self.nodes[i].getrawmempool())}", self.nodes, DEBUG_MODE)
            if i != DOUBLE_SPEND_NODE_INDEX:
                assert_equal(len(self.nodes[i].getrawmempool()), iterations)
            else:
                assert_equal(len(self.nodes[i].getrawmempool()), 1)

        mark_logs(f"Node {DOUBLE_SPEND_NODE_INDEX} mines a block containing this transaction", self.nodes, DEBUG_MODE)
        self.nodes[DOUBLE_SPEND_NODE_INDEX].generate(1)

        mark_logs(("Join the network and sync; all the nodes should remove the pending transactions from mempool"
                   "(conflicting with the transaction that has just been mined)"), self.nodes, DEBUG_MODE)
        self.join_network()
        self.sync_all()

        mark_logs("Check that the mempool of every node is empty", self.nodes, DEBUG_MODE)
        for i in range(NUMB_OF_NODES):
            mark_logs(f"Node {i} mempool size: {len(self.nodes[i].getrawmempool())}", self.nodes, DEBUG_MODE)
            assert_equal(len(self.nodes[i].getrawmempool()), 0)

        mark_logs(f"Current balance of the sending address: {self.nodes[MAIN_NODE].z_getbalance(node0_address):.8f}", self.nodes, DEBUG_MODE)

        # Check the consistency of all "getbalance" RPC commands
        for i in range(NUMB_OF_NODES):
            node_amount = self.nodes[i].getbalance()
            assert_equal(node_amount, self.nodes[i].getbalance(""))
            assert_equal(node_amount, self.nodes[i].getbalance("*"))
            assert_equal(node_amount, Decimal(self.nodes[i].z_gettotalbalance()["total"]))

if __name__ == '__main__':
    TxnMallTest().main()
