#!/usr/bin/env python3
# Copyright (c) 2016 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


from test_framework.test_framework import BitcoinTestFramework, ForkHeights
from test_framework.util import assert_equal, assert_greater_than, initialize_chain_clean, \
                                start_nodes, sync_blocks, connect_nodes_bi, wait_and_assert_operationid_status

Z_FEE = 0.0001

class Test (BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 2)

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir, [['-experimentalfeatures', '-zmergetoaddress']] * 2)
        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split=False
        self.sync_all()

    def make_all_mature(self):
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

    def run_test (self):

        '''
        In this test we want to check that a tx becoming irrevocably invalid after hard-fork overcome
        is actually removed from mempool
        '''

        # ####################################################################################################
        # fork11 - shielded pool deprecation hard fork
        # ####################################################################################################

        ForkHeight = ForkHeights['SHIELDED_POOL_DEPRECATION']

        self.nodes[0].generate(10) # for having some funds
        self.sync_all()

        block_count = self.nodes[0].getblockcount()
        self.nodes[1].generate(ForkHeight - block_count - 3)
        self.sync_all()

        # split the network in order have the shielding tx only on one node
        self.split_network(0)
        
        total_funds_before_shielding = sum(x['amount'] for x in self.nodes[0].listunspent(0))
        opid = self.nodes[0].z_mergetoaddress(["ANY_TADDR"], self.nodes[0].z_getnewaddress(), Z_FEE, 2, 2)["opid"]
        shielding_tx = wait_and_assert_operationid_status(self.nodes[0], opid)
        assert_greater_than(total_funds_before_shielding, sum(x['amount'] for x in self.nodes[0].listunspent(0))) # some less funds available

        # rejoin the network and check the shielding tx is only on one node
        self.join_network(0)
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 1)
        assert_equal(self.nodes[1].getmempoolinfo()["size"], 0)
        
        # then start mining on the other node

        self.nodes[1].generate(1)
        sync_blocks(self.nodes)
        # expecting shielding tx not yet being evicted
        assert_equal(self.nodes[0].getblockcount(), ForkHeight - 2)
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 1)
        assert_equal(self.nodes[1].getmempoolinfo()["size"], 0)

        self.nodes[1].generate(1)
        sync_blocks(self.nodes)
        # expecting shielding tx being evicted
        # (because at "ForkHeight - 1" any shielding tx still in mempool would not be mined)
        assert_equal(self.nodes[0].getblockcount(), ForkHeight - 1)
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 0)
        assert_equal(self.nodes[1].getmempoolinfo()["size"], 0)

        # check that even resending unconfirmed txs, the shielding tx does not get resent
        resenttxs = self.nodes[0].resendwallettransactions()
        assert_equal(len(resenttxs), 0)

        # check that the funds are back available for being sent in another tx
        assert_equal(sum(x['amount'] for x in self.nodes[0].listunspent(0)), total_funds_before_shielding) # funds back available
        self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), total_funds_before_shielding, "", "", True)
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 1)
        self.sync_all()


if __name__ == '__main__':
    Test().main()