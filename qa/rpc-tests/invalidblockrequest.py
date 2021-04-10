#!/usr/bin/env python2
#
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#

from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import assert_equal, start_nodes
from test_framework.comptool import TestManager, TestInstance
from test_framework.mininode import NetworkThread
from test_framework.blocktools import create_block, get_nBits, create_coinbase_h, create_transaction

import copy
import time


'''
In this test we connect to one node over p2p, and test block requests:
1) Valid blocks should be requested and become chain tip.
2) Invalid block with duplicated transaction should be re-requested.
3) Invalid block with bad coinbase value should be rejected and not
re-requested.
'''

# Use the ComparisonTestFramework with 1 node: only use --testbinary.
class InvalidBlockRequestTest(ComparisonTestFramework):

    ''' Can either run this test as 1 node with expected answers, or two and compare them.
        Change the "outcome" variable from each TestInstance object to only do the comparison. '''
    def __init__(self):
        self.num_nodes = 1

    def run_test(self):
        test = TestManager(self, self.options.tmpdir)
        test.add_all_connections(self.nodes)
        self.tip = None
        self.block_time = None
        NetworkThread().start() # Start up network handling in another thread
        test.run()

    def setup_network(self):
        self.nodes = start_nodes(self.num_nodes, self.options.tmpdir,
                                    extra_args=[[
                                        '-logtimemicros=1',
                                        '-debug=pow',
                                        '-debug=net',
                                        '-debug=rpc',
                                        '-whitelist=127.0.0.1']] * self.num_nodes,
                                    binary=[self.options.testbinary] +
                                           [self.options.refbinary]*(self.num_nodes-1))

    def get_tests(self):
        if self.tip is None:
            self.tip = int ("0x" + self.nodes[0].getbestblockhash() + "L", 0)
        self.block_time = int(time.time())+1

        chainHeight = 0

        '''
        Create a new block with an anyone-can-spend coinbase
        '''
        block = create_block(self.tip, create_coinbase_h(chainHeight+1), self.block_time)
        chainHeight += 1
        self.block_time += 1
        block.solve()
        # Save the coinbase for later
        self.block1 = block
        self.tip = block.sha256
        yield TestInstance([[block, True]])

        '''
        Now we need that block to mature so we can spend the coinbase.
        '''
        test = TestInstance(sync_every_block=False)
        for i in xrange(100):
            block = create_block(self.tip, create_coinbase_h(chainHeight+1), self.block_time, get_nBits(chainHeight))
            chainHeight += 1
            block.solve()
            self.tip = block.sha256
            self.block_time += 1
            test.blocks_and_transactions.append([block, True])
        yield test

        '''
        Now we use merkle-root malleability to generate an invalid block with
        same blockheader.
        Manufacture a block with 3 transactions (coinbase, spend of prior
        coinbase, spend of that spend).  Duplicate the 3rd transaction to 
        leave merkle root and blockheader unchanged but invalidate the block.
        '''
        block2 = create_block(self.tip, create_coinbase_h(chainHeight+1), self.block_time, get_nBits(chainHeight))
        chainHeight += 1
        self.block_time += 1

        # chr(81) is OP_TRUE
        tx1 = create_transaction(self.block1.vtx[0], 0, chr(81), 4*100000000)
        tx2 = create_transaction(tx1, 0, chr(81), 4*100000000)

        block2.vtx.extend([tx1, tx2])
        block2.hashMerkleRoot = block2.calc_merkle_root()
        block2.rehash()
        block2.solve()
        orig_hash = block2.sha256
        block2_orig = copy.deepcopy(block2)

        # Mutate block 2
        block2.vtx.append(tx2)
        assert_equal(block2.hashMerkleRoot, block2.calc_merkle_root())
        assert_equal(orig_hash, block2.rehash())
        assert(block2_orig.vtx != block2.vtx)

        self.tip = block2.sha256
        yield TestInstance([[block2, False], [block2_orig, True]])

        '''
        Make sure that a totally screwed up block is not valid.
        '''
        block3 = create_block(self.tip, create_coinbase_h(chainHeight+1), self.block_time, get_nBits(chainHeight))
        chainHeight += 1
        self.block_time += 1
        block3.vtx[0].vout[0].nValue = 100*100000000 # Too high!
        block3.vtx[0].sha256=None
        block3.vtx[0].calc_sha256()
        block3.hashMerkleRoot = block3.calc_merkle_root()
        block3.rehash()
        block3.solve()

        yield TestInstance([[block3, False]])


if __name__ == '__main__':
    InvalidBlockRequestTest().main()
