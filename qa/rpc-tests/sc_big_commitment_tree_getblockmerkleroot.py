#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import initialize_chain_clean, \
     start_nodes, assert_false, mark_logs
from test_framework.test_framework import ForkHeights, JSONRPCException

NUMB_OF_NODES = 1
DEBUG_MODE = 1
FT_LIMIT = 4095     # Keep this aligned with CCTPlib

class BigCommitmentTree_getblockmerkleroot(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[['-logtimemicros=1', '-debug=sc', '-debug=py', '-debug=cert']] * NUMB_OF_NODES)
        self.is_network_split = split

    def run_test(self):
        '''
        1. Mine blocks until reaching the sidechain fork; initialize tx_data with valid transaction data
        2. Create a raw tx
        3. Generate merkleTree and scTxsCommitmentTree for (FT_LIMIT - 1), FT_LIMIT and (FT_LIMIT + 1) txs (current FT_LIMIT is 4095)
        4. Check if the ScTxsCommitmentTree stops updating due to limited height
        '''

        # Reach SC v2 fork height
        mark_logs("Generating enough blocks to reach SC fork", self.nodes, DEBUG_MODE, 'e')
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])

        # Valid addresses...
        return_node_address = 'ztiQcTDe5sKctYZimWcZbkJY6nFQtQmMzcC'
        scid = '041d1207045d3a03e957a4dec777fa74d66c1f3c8b9ee02a2092ef7cf74a5f51'
        amount = 0.001
        address = "aabb"
        tx_data = {"address": address, "amount": amount, "scid": scid, "mcReturnAddress": return_node_address}

        # No need to create a sidechain, getblockmerkleroots works anyway
        rawtx = self.nodes[0].createrawtransaction([],{},[], [], [tx_data])

        # Evaluate scCommitment tree before and across the limit
        mark_logs(f"Evaluating merkle root and txs commitment for a tx with {(FT_LIMIT - 1)} FT", self.nodes, DEBUG_MODE, 'g')
        merk_limit_1 = self.nodes[0].getblockmerkleroots([rawtx]*(FT_LIMIT - 1),[])
        mark_logs(f"Evaluating merkle root and txs commitment for a tx with {FT_LIMIT} FT", self.nodes, DEBUG_MODE, 'g')
        merk_limit = self.nodes[0].getblockmerkleroots([rawtx]*FT_LIMIT,[])
        assert_false(merk_limit_1['scTxsCommitment'] == merk_limit['scTxsCommitment'])    # Under normal assumptions, these must be different

        # Now this will fail
        try:
            mark_logs(f"Evaluating merkle root and txs commitment for a tx with {(FT_LIMIT + 1)} FT (must fail)", self.nodes, DEBUG_MODE, 'y')
            merk_over = self.nodes[0].getblockmerkleroots([rawtx]*(FT_LIMIT + 1),[])
            mark_logs("getblockmerkleroots() did not fail (that's bad)", self.nodes, DEBUG_MODE, 'r')
            assert(False)
        except JSONRPCException as e:
            print(e.error['message'])
            

if __name__ == '__main__':
    BigCommitmentTree_getblockmerkleroot().main()