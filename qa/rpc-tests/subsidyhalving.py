#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, stop_nodes, wait_bitcoinds, sync_blocks, sync_mempools, connect_nodes_bi, \
    connect_nodes, wait_and_assert_operationid_status
import os
import pprint
from decimal import *

DEBUG_MODE = 1
NUMB_OF_NODES = 3

# TODO set to 2000 when done
#HALVING_INTERVAL = 500
HALVING_INTERVAL = 2000

class subsidyhalving(BitcoinTestFramework):

    alert_filename = None

    def mark_logs(self, msg):
        print msg
        for i in range(0, NUMB_OF_NODES):
            self.nodes[i].dbg_log(msg)

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False, mod_halving_interval=True):
        self.nodes = []

        halving_interv = HALVING_INTERVAL
        if mod_halving_interval:
            halving_interv = HALVING_INTERVAL + 10 

        print "Node0 will start with -subsidyhalvinginterval={}".format(halving_interv)

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
            extra_args = [
                ['-logtimemicros', '-debug=py', '-debug=net', '-subsidyhalvinginterval=%d'%halving_interv],
                ['-logtimemicros', '-debug=py', '-debug=net'],
                ['-logtimemicros', '-debug=py', '-debug=net']
            ])

        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[1], 2)
        connect_nodes(self.nodes[2], 0)
        sync_blocks(self.nodes, 1, False, 5)
        sync_mempools(self.nodes[0:NUMB_OF_NODES])
        self.is_network_split = split


    def run_test(self):

        """
         miner reward
        -------------------------------------------
               fork
        -------------------------------------------
        Height    0,.., 100 --> reward: 11.43750000
        Height  101,.., 104 --> reward: 11.00000000
        Height  105,.., 199 --> reward:  8.75000000
        Height  200,..,1999 --> reward:  7.50000000
        Height 2000,..,3999 --> reward:  3.75000000 (1st halving)
        Height 4000,..,5999 --> reward:  1.87500000 (2nd halving)
                                                ...
        """
        COINBASE_AMOUNT = Decimal("12.50000000")

        MINER_REWARD_0    = Decimal("11.4375")
        MINER_REWARD_101  = Decimal("11.0000")
        MINER_REWARD_105  = Decimal("8.7500")
        MINER_REWARD_200  = Decimal("7.5000")
        MINER_REWARD_2000 = Decimal("3.7500")
        MINER_REWARD_4000 = Decimal("1.8750")

        def check_coinbase(node, block, reward_amount, coinbase_amount):
            # check we have expected coinbase amount
            coinbase = node.getblock(block, True)['tx'][0]
            decoded_coinbase = node.getrawtransaction(coinbase, 1)
            miner_quota       = decoded_coinbase['vout'][0]['value']
            assert_equal(miner_quota, reward_amount)
            tot = miner_quota
            print "Height {:>5} ---> miner reward:       {:>12}".format(node.getblockcount(), miner_quota)

            if len(decoded_coinbase['vout']) > 1:
                comm_quota        = decoded_coinbase['vout'][1]['value']
                print "                  community quota:    {:>12}".format(comm_quota)
                tot += comm_quota
            if len(decoded_coinbase['vout']) > 2:
                supernodes_quota  = decoded_coinbase['vout'][2]['value']
                print "                  super nodes quota:  {:>12}".format(supernodes_quota)
                tot += supernodes_quota
            if len(decoded_coinbase['vout']) > 3:
                securenodes_quota = decoded_coinbase['vout'][3]['value']
                print "                  secure nodes quota: {:>12}".format(securenodes_quota)
                tot += securenodes_quota

            assert_equal(tot, coinbase_amount)
            print "                                      ------------"
            print "               COINBASE TOT AMOUNT:   {:>12}".format(coinbase_amount)
            print

        def check_fork_reward(node, numb_of_blocks, reward_amount, coinbase_amount = COINBASE_AMOUNT):

            block = node.generate(1)[0]
            #self.sync_all()
            sync_blocks(self.nodes, 1, False, 5)
            
            # check starting block
            check_coinbase(node, block, reward_amount, coinbase_amount)

            for i in range(1, numb_of_blocks-1):
                node.generate(1)

            block = node.generate(1)[0]
            #self.sync_all()
            sync_blocks(self.nodes, 1, False, 5)

            # check ending block
            check_coinbase(node, block, reward_amount, coinbase_amount)

 
        check_fork_reward(self.nodes[1],  100, MINER_REWARD_0)
        check_fork_reward(self.nodes[1],    4, MINER_REWARD_101)
        check_fork_reward(self.nodes[1],   95, MINER_REWARD_105)
#        check_fork_reward(self.nodes[1], 1800, MINER_REWARD_200)
        check_fork_reward(self.nodes[1], (HALVING_INTERVAL-200), MINER_REWARD_200)

        peer_info_0_pre = self.nodes[0].getpeerinfo()
        peer_info_1_pre = self.nodes[1].getpeerinfo()
        peer_info_2_pre = self.nodes[2].getpeerinfo()

        # checking all 3 nodes are connected (connections are unidirectional, 1 entry for each node)
        assert_equal(len(peer_info_0_pre), 2)
        assert_equal(len(peer_info_0_pre), len(peer_info_1_pre))
        assert_equal(len(peer_info_0_pre), len(peer_info_2_pre))

        self.mark_logs("Testing 1st halving...")

        self.mark_logs("Checking we reject blocks with pre-halving coinbase amount...")

        self.mark_logs("Node0 generate 1 block with pre-halving coinbase amount...")
        bad_block = self.nodes[0].generate(1)[0]
        check_coinbase(self.nodes[0], bad_block, MINER_REWARD_200, COINBASE_AMOUNT)

        # upon sync, the other nodes will reject this block and Node0 will be banned by both Node1 and Node2
        sync_blocks(self.nodes, 1, False, 5)

        peer_info_0_post = self.nodes[0].getpeerinfo()
        peer_info_1_post = self.nodes[1].getpeerinfo()
        peer_info_2_post = self.nodes[2].getpeerinfo()

        # checking node 0 is not connected anymore to the other nodes (connections are unidirectional, 1 entry for each node)
        assert_equal(len(peer_info_0_post), 0)
        assert_equal(len(peer_info_1_post), 1)
        assert_equal(len(peer_info_1_post), len(peer_info_2_post))

        check_fork_reward(self.nodes[1], 5, MINER_REWARD_2000, COINBASE_AMOUNT*Decimal("0.5"))
        
        # Node0 has just its own chain
        ct0 = self.nodes[0].getchaintips()
        assert_equal(len(ct0), 1)
        best_0 = self.nodes[0].getbestblockhash()

        # Node1 and Node2 sees Node0 fork as invalid
        ct1 = self.nodes[1].getchaintips()
        ct2 = self.nodes[2].getchaintips()
        assert_equal(len(ct1), 2)
        assert_equal(ct1, ct2)

        # and both sees as invalid chain tip the best block hash of Node0
        for k in ct1:
            if k['status'] == "invalid":
                assert_equal(k['hash'], best_0)


        #----------------------------------------------------------------------------------
        self.mark_logs("stopping and restarting nodes with default subsidyhalvinginterval value")
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False, False)

        # enhance active chain after halving
        check_fork_reward(self.nodes[2], 20, MINER_REWARD_2000, COINBASE_AMOUNT*Decimal("0.5"))

        # Node0 had its bad chain reverted and now all Nodes share the same blockchain with halved coinbase
        assert_equal(self.nodes[0].getbestblockhash(), self.nodes[1].getbestblockhash())
        assert_equal(self.nodes[0].getbestblockhash(), self.nodes[2].getbestblockhash())


if __name__ == '__main__':
    subsidyhalving().main()
