#!/usr/bin/env python3
#
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#

from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import assert_equal, start_nodes
from test_framework.comptool import TestManager, TestInstance
from test_framework.mininode import NetworkThread
from test_framework.blocktools import create_block, get_nBits, create_coinbase_h, create_transaction

#import copy
import time
import pprint
from decimal import Decimal


'''
In this test we connect to one node over p2p, and test block requests:
1) Valid blocks should be requested and become chain tip.
2) Invalid block with uncorrect subsidy quotas should be rejected
3) Invalid block with uncorrect coinbase value after halving height (set to SUBS_HALV_INTERVAL) should be rejected
'''

SUBS_HALV_INTERVAL=300

# Use the ComparisonTestFramework with 1 node: only use --testbinary.
class InvalidBlockPostHalving(ComparisonTestFramework):

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
                                        '-subsidyhalvinginterval=%d'%SUBS_HALV_INTERVAL,
                                        '-whitelist=127.0.0.1']] * self.num_nodes,
                                    binary=[self.options.testbinary] +
                                           [self.options.refbinary]*(self.num_nodes-1))

    def get_tests(self):
        if self.tip is None:
            self.tip = int ("0x" + self.nodes[0].getbestblockhash(), 0)
        self.block_time = int(time.time())+1

        chainHeight = 0

        def get_chaintip_subsidy_amount():
            best_hash = self.nodes[0].getbestblockhash()
            cb_txid = self.nodes[0].getblock(best_hash)['tx'][0]
            cb_tx = self.nodes[0].getrawtransaction(cb_txid, 1)
            subsidy = Decimal("0.0")  
            for x in cb_tx['vout']:
                subsidy += x['value']
            print(" CB amount = {}".format(subsidy))
            return subsidy

        print("---> Create a new block with an anyone-can-spend coinbase")

        block = create_block(self.tip, create_coinbase_h(chainHeight+1), self.block_time)
        chainHeight += 1
        self.block_time += 1
        block.solve()
        # Save the coinbase for later
        self.block1 = block
        self.tip = block.sha256
        yield TestInstance([[block, True]])

        # check testnode and zend node are synced
        assert_equal(self.nodes[0].getblockcount(), chainHeight)
        assert_equal(int ("0x" + self.nodes[0].getbestblockhash(), 0), self.tip) 

        cb_subsidy_pre_halving = get_chaintip_subsidy_amount()

        test = TestInstance(sync_every_block=False)
        print("---> We need that block to mature so we can spend the coinbase: creating 100 blocks")

        test = TestInstance(sync_every_block=False)
        for i in range(100):
            block = create_block(self.tip, create_coinbase_h(chainHeight+1), self.block_time, get_nBits(chainHeight))
            chainHeight += 1
            block.solve()
            self.tip = block.sha256
            self.block_time += 1
            test.blocks_and_transactions.append([block, True])
            if ((i+1) % 10) == 0:
                print("... {} blocks created".format(i+1))
        yield test

        # check testnode and zend node are synced
        assert_equal(self.nodes[0].getblockcount(), chainHeight)
        assert_equal(int ("0x" + self.nodes[0].getbestblockhash(), 0), self.tip) 

        print("---> Go beyond the fork that introduces other shares of coinbase for supernodes/securenodes, that is h=105 in regtest.")

        test = TestInstance(sync_every_block=False)
        for i in range(10):
            block = create_block(self.tip, create_coinbase_h(chainHeight+1), self.block_time, get_nBits(chainHeight))
            chainHeight += 1
            block.solve()
            self.tip = block.sha256
            self.block_time += 1
            test.blocks_and_transactions.append([block, True])
            if ((i+1) % 10) == 0:
                print("... {} blocks created".format(i+1))
        yield test

        # check testnode and zend node are synced
        assert_equal(self.nodes[0].getblockcount(), chainHeight)
        assert_equal(int ("0x" + self.nodes[0].getbestblockhash(), 0), self.tip) 

        print("---> Now we go beyond the next fork point setting a new subsidy share distribution")

        test = TestInstance(sync_every_block=False)
        for i in range(100):
            block = create_block(self.tip, create_coinbase_h(chainHeight+1), self.block_time, get_nBits(chainHeight))
            chainHeight += 1
            block.solve()
            self.tip = block.sha256
            self.block_time += 1
            test.blocks_and_transactions.append([block, True])
            if ((i+1) % 10) == 0:
                print("... {} blocks created".format(i+1))
        yield test

        # check testnode and zend node are synced
        assert_equal(self.nodes[0].getblockcount(), chainHeight)
        assert_equal(int ("0x" + self.nodes[0].getbestblockhash(), 0), self.tip) 

        print("---> Now we go at the brink of the halving height (set to {})".format(SUBS_HALV_INTERVAL))
        test = TestInstance(sync_every_block=False)
        num_of_bl = SUBS_HALV_INTERVAL - self.nodes[0].getblockcount() -1
        for i in range(num_of_bl):
            block = create_block(self.tip, create_coinbase_h(chainHeight+1, SUBS_HALV_INTERVAL), self.block_time, get_nBits(chainHeight))
            chainHeight += 1
            block.solve()
            #print " {}) block.nBits = {}".format(i, block.nBits)
            self.tip = block.sha256
            self.block_time += 1
            test.blocks_and_transactions.append([block, True])
            if ((i+1) % 10) == 0:
                print("... {} blocks created".format(i+1))
        yield test

        # check testnode and zend node are synced
        assert_equal(self.nodes[0].getblockcount(), chainHeight)
        assert_equal(int ("0x" + self.nodes[0].getbestblockhash(), 0), self.tip) 

        # check we still have full cb subsidy
        cb_subsidy = get_chaintip_subsidy_amount()
        assert_equal(cb_subsidy, cb_subsidy_pre_halving)

        print("---> Make sure that a block with pre-halving subsidy is rejected.")
        block3 = create_block(self.tip, create_coinbase_h(chainHeight+1, SUBS_HALV_INTERVAL +1 #force not trigger halving
            ), self.block_time, get_nBits(chainHeight))
        chainHeight += 1
        self.block_time += 1
        block3.solve()

        yield TestInstance([[block3, False]])

        assert_equal(self.nodes[0].getblockcount(), chainHeight-1)
        chainHeight -= 1

        print("---> Make sure that a block with too high a miner rewarding after halving is rejected.")
        block3 = create_block(self.tip, create_coinbase_h(chainHeight+1, SUBS_HALV_INTERVAL), self.block_time, get_nBits(chainHeight))
        chainHeight += 1
        self.block_time += 1
        block3.vtx[0].vout[0].nValue = int(block3.vtx[0].vout[0].nValue * 1.5) # Too high!
        block3.vtx[0].sha256=None
        block3.vtx[0].calc_sha256()
        block3.hashMerkleRoot = block3.calc_merkle_root()
        block3.rehash()
        block3.solve()

        yield TestInstance([[block3, False]])

        assert_equal(self.nodes[0].getblockcount(), chainHeight-1)
        chainHeight -= 1

        print("---> Make sure that a block with a correctly halved cb is accepted.")
        block3 = create_block(self.tip, create_coinbase_h(chainHeight+1, SUBS_HALV_INTERVAL), self.block_time, get_nBits(chainHeight))
        chainHeight += 1
        self.block_time += 1
        block3.solve()
        self.tip = block3.sha256

        yield TestInstance([[block3, True]])

        # check testnode and zend node are synced
        assert_equal(self.nodes[0].getblockcount(), chainHeight)
        assert_equal(int ("0x" + self.nodes[0].getbestblockhash(), 0), self.tip) 

        # check we have the expected halved cb subsidy
        cb_subsidy_post_halving = get_chaintip_subsidy_amount()
        assert_equal(cb_subsidy_post_halving, cb_subsidy_pre_halving * Decimal("0.5"))

        print("---> Make sure that a block with a miner rewarding less than expected is NOT rejected.")
        block3 = create_block(self.tip, create_coinbase_h(chainHeight+1, SUBS_HALV_INTERVAL), self.block_time, get_nBits(chainHeight))
        chainHeight += 1
        self.block_time += 1
        block3.vtx[0].vout[0].nValue = int(block3.vtx[0].vout[0].nValue * 0.5)
        block3.vtx[0].sha256=None
        block3.vtx[0].calc_sha256()
        block3.hashMerkleRoot = block3.calc_merkle_root()
        block3.rehash()
        block3.solve()
        self.tip = block3.sha256

        yield TestInstance([[block3, True]])

        # check testnode and zend node are synced
        assert_equal(self.nodes[0].getblockcount(), chainHeight)
        assert_equal(int ("0x" + self.nodes[0].getbestblockhash(), 0), self.tip) 

        # check we have the expected less-than halved cb subsidy
        cb_subsidy = get_chaintip_subsidy_amount()
        assert(cb_subsidy  < cb_subsidy_pre_halving * Decimal("0.5"))




if __name__ == '__main__':
    InvalidBlockPostHalving().main()
