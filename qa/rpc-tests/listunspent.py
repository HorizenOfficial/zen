#!/usr/bin/env python3
# Copyright (c) 2024 The Horizen Foundation
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, start_nodes, initialize_chain_clean, connect_nodes

ADDITIONAL_TXS = 10

class ListUnspentTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4)

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir)
        connect_nodes(self.nodes,0,1)
        self.is_network_split=False
        self.sync_all()

    def run_test(self):
        [alice, miner] = self.nodes

        addr_alice = alice.getnewaddress()
        addr_alice_2 = alice.getnewaddress()
        addr_miner = miner.getnewaddress()

        miner.generate(101) # need to have spendeable coinbase
        txid = miner.sendtoaddress(addr_alice, Decimal('1.0'))
        miner.generate(10)
        self.sync_all()

        def have_tx(txid, listunspent):
            for tx in listunspent:
                if tx['txid'] == txid:
                    return True
            return False

        # Check that txid is the only tx in alice's wallet
        unspent = alice.listunspent()
        assert(len(unspent) == 1 and unspent[0]['txid'] == txid)

        # Check that parameters are used correctly 
        unspent = alice.listunspent(10, 10, [addr_alice])
        assert(len(unspent) == 1 and unspent[0]['txid'] == txid)

        # Check that minDepth is respected (maxDepth is 9999999 as per documentation)
        unspent = alice.listunspent(11, 9999999, [])
        assert_equal(len(unspent), 0)

        # Check that maxDepth is respected (minDepth is 1 as per documentation)
        unspent = alice.listunspent(1, 9, [])
        assert_equal(len(unspent), 0)

        # Check that address is respected
        unspent = alice.listunspent(1, 9999999, [addr_alice_2])
        assert_equal(len(unspent), 0)

        for tx in range(ADDITIONAL_TXS):
            txid = miner.sendtoaddress(addr_alice_2, Decimal('1.0'))
            miner.generate(1)
            self.sync_all()
            assert(have_tx(txid, alice.listunspent()))

        unspent = alice.listunspent()
        assert_equal(len(unspent), ADDITIONAL_TXS + 1)
        assert(have_tx(txid, unspent))

        # Check that locked transactions are not returned by listunspent
        lock_tx = {"txid": unspent[0]['txid'], "vout": unspent[0]['vout']}
        assert(alice.lockunspent(False, [lock_tx]))
        unspent = alice.listunspent()
        assert_equal(len(unspent), ADDITIONAL_TXS)
        assert(not have_tx(lock_tx['txid'], unspent))

        # Check that spent transactions are not returned by listunspent
        alice.lockunspent(True, [lock_tx])
        alice.sendtoaddress(addr_miner, Decimal(alice.getbalance()), "", "", True)
        unspent = alice.listunspent()
        assert_equal(len(unspent), 0)


if __name__ == '__main__':
    ListUnspentTest().main()
