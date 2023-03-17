#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from decimal import Decimal
import time

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, \
     start_nodes, connect_nodes_bi, assert_true, assert_false, mark_logs
from test_framework.test_framework import ForkHeights, JSONRPCException
from test_framework.blockchainhelper import BlockchainHelper, SidechainParameters

NUMB_OF_NODES = 2
DEBUG_MODE = 1
EPOCH_LENGTH = 10
CERT_FEE = Decimal('0.0001')

'''
This test excercises the txs commitment builder.
CCTPlib stores the txs commitment tree as follows:
- information for alive sidechains and ceased sidechains are stored on different subtrees
- the txs commitment tree can contain up to 4096 subtrees (alive + ceased), each one for a different sidechain
- each alive sidechain subtree has 3 subtrees, each one containing Forward Transfers, Backward Transfer Requests
  and Certificates hashes in their leaves
- each ceased sidechain subtree has 1 subtree containing Ceased Sidechain Withdrawal hashed in its leaves
- FT / BWTR / CERT / CSW can contain up to 4095 leaves
- Sidechain Creation transaction is NOT stored in a leaf, but in a separate field in the Rust structure that models
  the txs commitment tree

CCTPlib txs commitment builder fails when:
- adding FTs / BWTRs / CERTs / CSWs belonging to more than 4096 different sidechains
- adding more than 4095 FTs for a SC
- adding more than 4095 BWTRs for a SC
- adding more than 4095 CERTs for a SC
- adding more than 4095 CSWs for a SC
- adding a FT / BWTR / CERT for a SC after a CSW for the same SC has been added
- adding a CSW for a SC after a FT / BWTR / CERT for the same SC has been added
- internal hashing issues

Currently, CommitmentBuilderGuard manages all these failure reasons but the last one.

In this test, we call getblocktemplate() so that a txs commitment tree is built from the txs that are stored in the mempool of a node
[CreateNewBlock(..) and related function in zend]
1. we create a set of 4 transactions having in total 4094 FTs for a single SC; getblocktemplate() will not fail and returns a valid block
     mempool <- {tx1[1024FT], tx2[1024FT], tx3[1024FT], tx4[1022FT]}
     getblocktemplate() returns a block with [tx1[1024FT], tx2[1024FT], tx3[1024FT], tx4[1022FT]]
2. adding another tx having 2 FTs for the same sc to the mempool will make getblocktemplate() ignore the newly added tx, as its addiction
   makes the commitmentTreeBuilder fail. getblocktemplate returns the same block
     mempool <- {tx1[1024FT], tx2[1024FT], tx3[1024FT], tx4[1022FT], tx5[2FT]}
     getblocktemplate() returns a block with [tx1[1024FT], tx2[1024FT], tx3[1024FT], tx4[1022FT]]
3. adding a single tx having a single FT for the same sc to the mempool does not make the commitmentTreeBuilder fail and the block
   returned by getblocktemplate() will include the 4 txs of 1. and this newly added txs. tx of 2. is still rejected
     mempool <- {tx1[1024FT], tx2[1024FT], tx3[1024FT], tx4[1022FT], tx5[2FT], tx6[1FT]}
     getblocktemplate() returns a block with [tx1[1024FT], tx2[1024FT], tx3[1024FT], tx4[1022FT], tx6[1FT]]
4. adding a single tx having a single FT for the same sc to the mempool will now make the commitmentTreeBuilder fail.
     mempool <- {tx1[1024FT], tx2[1024FT], tx3[1024FT], tx4[1022FT], tx5[2FT], tx6[1FT], tx7[1FT]}
     getblocktemplate() returns a block with [tx1[1024FT], tx2[1024FT], tx3[1024FT], tx4[1022FT], tx6[1FT]]
5. We mine a block WITHOUT calling the getblocktemplate and confirm that only tx5 and tx7 are still in mempool
     mempool <- {tx5[2FT], tx7[1FT]}
'''

class BigCommitmentTree(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[['-logtimemicros=1', '-debug=sc', '-debug=py', '-debug=mempool', '-debug=cert']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    # This function waits until the getblocktemplate() command returns the given number of transactions
    # within a specified time frame (default = 10 seconds, set to 0 seconds to wait indefinitely)
    def wait_for_block_template(self, node_id, expected_num_transactions, timeout = 10):
        delay = 1
        iteration = 0

        while timeout > 0 and iteration < timeout // delay:
            current_block_template = self.nodes[node_id].getblocktemplate({}, True)

            if len(current_block_template['transactions']) == expected_num_transactions:
                return current_block_template

            time.sleep(delay)
            iteration = iteration + 1

        return None

    def run_test(self):
        # Reach SC v2 fork height
        mark_logs("Generating enough blocks to reach non-ceasing SC fork", self.nodes, DEBUG_MODE, 'e')
        self.nodes[0].generate(ForkHeights['NON_CEASING_SC'])
        self.sync_all()

        test_helper = BlockchainHelper(self)

        mark_logs("Node 0 creates a v2 sidechain (ceasing)", self.nodes, DEBUG_MODE, 'e')
        test_helper.create_sidechain("test_sidechain", SidechainParameters["DEFAULT_SC_V2_CEASABLE"])
        sc_id = test_helper.get_sidechain_id("test_sidechain")
        mark_logs(f"Sidechain {sc_id} has been created", self.nodes, DEBUG_MODE, 'e')

        self.nodes[0].generate(1)
        self.sync_all()

        # Send some coins to Node1
        # By using a new address for Node1, we are sure that any transaction created by Node1
        # will use the change of the previous one, forcing the getblocktemplate() function
        # to sort transaction based on submission order (because any transaction will depend
        # on the previous one)
        node1_address = self.nodes[1].getnewaddress()
        self.nodes[0].sendtoaddress(node1_address, 100)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        current_sc_txs_commitment_tree_root = ""

        forwardTransferOuts = [{'toaddress': "aabb", 'amount': Decimal(0.001), "scid": sc_id, "mcReturnAddress": node1_address}]

        # Add to mempool 4 transactions, each containing 1024, 1024, 1024 and 1022 forward transfers (total 4094,
        # which is almost the limit size of ScTxsCommTree)
        mark_logs(f"Adding 4094 FT for sc {sc_id} to the mempool (4 transactions)", self.nodes, DEBUG_MODE, 'g')
        multipll = [1024, 1024, 1024, 1022]
        for i in range(4):
            self.nodes[1].sc_send(forwardTransferOuts * multipll[i], {"fee": Decimal(0.0001 * (i + 1)), "minconf": 0})
            mark_logs(f"Sent transaction #{i + 1}", self.nodes, DEBUG_MODE, 'c')
            self.sync_all()

            block_template = self.wait_for_block_template(0, i + 1)
            assert(block_template is not None)
            mark_logs(f"getblocktemplate() ->", self.nodes, DEBUG_MODE, 'e')
            for k, transaction in enumerate(block_template['transactions']):
                mark_logs(f"  {k} - {transaction['hash']}", self.nodes, DEBUG_MODE, 'e')
            mark_logs(f"    ScTxsCommitmentTree root: {block_template['scTxsCommitment']}", self.nodes, DEBUG_MODE, 'e')

            assert_false(current_sc_txs_commitment_tree_root == block_template['scTxsCommitment'])
            current_sc_txs_commitment_tree_root = block_template['scTxsCommitment']


        # Let's try to add two more forward transfers to the mempool to exceed ScTxsCommTree capacity
        mark_logs(f"Adding 2 FT for sc {sc_id} to the mempool", self.nodes, DEBUG_MODE, 'y')
        try:
            exceeding_tx = self.nodes[1].sc_send(forwardTransferOuts * 2, {"fee": Decimal(0.0001 * 5), "minconf": 0})
        except JSONRPCException as e:
            print(e.error['message'])
            assert(False)
        mark_logs(f"Sent transaction FT #4095 and #4096", self.nodes, DEBUG_MODE, 'c')
        mark_logs(f"  4 - {exceeding_tx}", self.nodes, DEBUG_MODE, 'e')
        self.sync_all()

        # Ask for a new block template
        block_template = self.wait_for_block_template(0, i + 1)
        assert(block_template is not None)
        mark_logs(f"getblocktemplate() ->", self.nodes, DEBUG_MODE, 'e')
        for k, transaction in enumerate(block_template['transactions']):
            mark_logs(f"  {k} - {transaction['hash']}", self.nodes, DEBUG_MODE, 'e')
        mark_logs(f"    ScTxsCommitmentTree root: {block_template['scTxsCommitment']}", self.nodes, DEBUG_MODE, 'e')

        # The commitment tree should be the same as in the previous step because we rejected the last TX.
        assert_equal(current_sc_txs_commitment_tree_root, block_template['scTxsCommitment'])
        # Also, exceeding_tx should not have been included in the block
        assert_false(exceeding_tx in [tx['hash'] for tx in block_template['transactions']], "The last TX should not be included in the block candidate!!!")


        # Try again, but this time we only send the 4095th FT, so that it will be included in the block candidate
        # We use createrawtransaction to avoid dependencies to the previous tx
        mark_logs(f"Adding 1 FT for sc {sc_id} to the mempool", self.nodes, DEBUG_MODE, 'g')
        try:
            tx_data = {"address": "aabb", "amount": 0.003, "scid": sc_id, "mcReturnAddress": node1_address}
            rawtx = self.nodes[0].createrawtransaction([],{},[], [], [tx_data])
            fundedtx = self.nodes[0].fundrawtransaction(rawtx)
            sigRawtx = self.nodes[0].signrawtransaction(fundedtx['hex'])
            last_tx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException as e:
            print(e.error['message'])
            assert(False)
        mark_logs(f"Sent transaction FT #4095", self.nodes, DEBUG_MODE, 'c')
        mark_logs(f"  5 - {last_tx}", self.nodes, DEBUG_MODE, 'e')
        self.sync_all()

        # Ask for a new block template
        block_template = self.wait_for_block_template(0, i + 2)
        assert(block_template is not None)
        current_sc_txs_commitment_tree_root = block_template['scTxsCommitment']
        # Check it has been included in the block candidate
        assert_true(last_tx in [tx['hash'] for tx in block_template['transactions']], "The last TX should be included in the block candidate!!!")
        mark_logs(f"getblocktemplate() ->", self.nodes, DEBUG_MODE, 'e')
        for k, transaction in enumerate(block_template['transactions']):
            mark_logs(f"  {k} - {transaction['hash']}", self.nodes, DEBUG_MODE, 'e')
        mark_logs(f"    ScTxsCommitmentTree root: {block_template['scTxsCommitment']}", self.nodes, DEBUG_MODE, 'e')


        # Try again adding an exceeding tx to the block candidate
        mark_logs(f"Adding 1 FT for sc {sc_id} to the mempool", self.nodes, DEBUG_MODE, 'y')
        try:
            exceeding_tx2 = self.nodes[1].sc_send(forwardTransferOuts, {"fee": Decimal(0.0001 * 7), "minconf": 0})
        except JSONRPCException as e:
            print(e.error['message'])
            assert(False)
        mark_logs(f"Sent transaction FT #4096", self.nodes, DEBUG_MODE, 'c')
        mark_logs(f"  6 - {exceeding_tx2}", self.nodes, DEBUG_MODE, 'e')
        self.sync_all()

        # Ask for a new block template
        block_template = self.wait_for_block_template(0, i + 2)
        assert(block_template is not None)
        mark_logs(f"getblocktemplate() ->", self.nodes, DEBUG_MODE, 'e')
        for k, transaction in enumerate(block_template['transactions']):
            mark_logs(f"  {k} - {transaction['hash']}", self.nodes, DEBUG_MODE, 'e')
        mark_logs(f"    ScTxsCommitmentTree root: {block_template['scTxsCommitment']}", self.nodes, DEBUG_MODE, 'e')

        # The commitment tree should be the same as in the previous step because we rejected the last TX.
        assert_equal(current_sc_txs_commitment_tree_root, block_template['scTxsCommitment'])
        # Also, exceeding_tx2 should not have been included in the block
        assert_false(exceeding_tx2 in [tx['hash'] for tx in block_template['transactions']], "The last TX should not be included in the block candidate!!!")


        # Now we mine the block that includes the 5 valid txs, then we make sure that exceeding_tx and exciding_tx2 is still in mempool
        mark_logs(f"Mine a block", self.nodes, DEBUG_MODE, 'c')
        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs(f"Confirm that only the exceeding txs are still in mempool", self.nodes, DEBUG_MODE, 'g')
        a = self.nodes[1].getrawmempool(True)
        assert_equal(len(a), 2)
        assert_true(exceeding_tx  in a, "tx {exceeding_tx} should still be in node 1 mempool")
        assert_true(exceeding_tx2 in a, "tx {exceeding_tx2} should still be in node 1 mempool")

        # Mine a block to clean the mempool (and make sure that exceeding_tx and exceeding_tx2 have been included in the block)
        mark_logs(f"Mine a block", self.nodes, DEBUG_MODE, 'c')
        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs(f"Confirm that the exceeding txs have been included in a block", self.nodes, DEBUG_MODE, 'g')
        a = self.nodes[1].getrawmempool(True)
        assert_equal(len(a), 0)

        # final test: a tx breaking the rules should never be accepted
        #(it implicitly violates the max size)
        mark_logs(f"Sending a big txs that violates the rules", self.nodes, DEBUG_MODE, 'y')
        try:
            self.nodes[1].sc_send(forwardTransferOuts * 4096, {"fee": Decimal(0.004), "minconf": 0})
            assert(False)
        except JSONRPCException as e:
            print(e.error['message'])

if __name__ == '__main__':
    BigCommitmentTree().main()
