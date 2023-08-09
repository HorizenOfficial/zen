#!/usr/bin/env python3
# Copyright (c) 2017 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_node, connect_nodes, COIN

import time

class PrioritiseTransactionTest (BitcoinTestFramework):

    blockprioritysize = 7000
    timeout_seconds = 30

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4)

    def setup_network(self, split=False):
        self.nodes = []
        # Start nodes with tiny block size of 11kb
        self.nodes.append(start_node(0, self.options.tmpdir, [f"-blockprioritysize={self.blockprioritysize}", "-blockmaxsize=11000", "-maxorphantx=1000", "-relaypriority=true", "-printpriority=1"]))
        self.nodes.append(start_node(1, self.options.tmpdir, [f"-blockprioritysize={self.blockprioritysize}", "-blockmaxsize=11000", "-maxorphantx=1000", "-relaypriority=true", "-printpriority=1"]))
        connect_nodes(self.nodes, 1, 0)
        self.is_network_split=False
        self.sync_all()

    def get_sorted_by(self, dict, priority_otherwise_fee):
        newdict = {}
        for key in dict:
            newdict[key] = {}
            newdict[key]["total_priority"] = dict[key]["currentpriority"]
            newdict[key]["total_fee"] = dict[key]["fee"]
            newdict[key]["size"] = dict[key]["size"]
            if ("priority_delta" in dict[key]):
                newdict[key]["total_priority"] += dict[key]["priority_delta"]
            if ("fee_delta" in dict[key]):
                newdict[key]["total_fee"] += dict[key]["fee_delta"]
        return sorted(newdict.items(), key=lambda x: x[1]["total_priority" if priority_otherwise_fee else "total_fee"], reverse=True)
    
    def get_max_priority_and_max_fee(self, rawmempool):
        sorted_rawmempool = self.get_sorted_by(rawmempool, True)
        max_priority = sorted_rawmempool[0][1]["total_priority"]
        sorted_rawmempool = self.get_sorted_by(rawmempool, False)
        max_fee = sorted_rawmempool[0][1]["total_fee"]
        return max_priority, max_fee

    def get_last_tx_selectable_by_priority(self, rawmempool):
        sorted_rawmempool = self.get_sorted_by(rawmempool, True)
        block_incremental_size = 1000 # check CreateNewBlock for this value
        last_tx_selectable_by_priority = None
        for index in range(len(sorted_rawmempool)):
            tx_size = sorted_rawmempool[index][1]["size"]
            last_tx_selectable_by_priority = sorted_rawmempool[index][0]
            if (block_incremental_size + tx_size >= self.blockprioritysize or
                sorted_rawmempool[index][1]["total_priority"] <= 100000000 * 144 / 250): # check AllowFreeThreshold for this value
                break
            block_incremental_size += tx_size
        return last_tx_selectable_by_priority

    def check_if_included_by_priority(self, tx, last_tx_by_priority, block_template):
        tx_included_by_priority = False
        tx_included = False
        last_tx_by_priority_included = False
        for index in range(len(block_template["transactions"])):
            if (block_template["transactions"][index]["hash"] == last_tx_by_priority):
                last_tx_by_priority_included = True
            if (block_template["transactions"][index]["hash"] == tx):
                tx_included = True
                if (not last_tx_by_priority_included):
                    tx_included_by_priority = True
                break
        return tx_included_by_priority, tx_included

    def check_if_mined_by_priority(self, tx, last_tx_by_priority, block):
        tx_mined_by_priority = False
        tx_mined = False
        last_tx_by_priority_mined = False
        for index in range(len(block["tx"])):
            if (block["tx"][index] == last_tx_by_priority):
                last_tx_by_priority_mined = True
            if (block["tx"][index] == tx):
                tx_mined = True
                if (not last_tx_by_priority_mined):
                    tx_mined_by_priority = True
                break
        return tx_mined_by_priority, tx_mined

    def run_test (self):

        '''
        This test checks that transactions being prioritized (by priority or by fee) are actually included in next blocktemplate/block
        by the requested criterion (precisely, by priority or by fee).
        It is worth to mention that blocktemplate algorithm selects transactions based on two steps: in the first step, transactions
        are listed by descending priority and as many transactions as can fit into the first portion of blocktemplate ("-blockprioritysize")
        are taken; in the second step, transactions are listed by descending fee and as many transactions as can fit into the remaining
        portion of blocktemplate ("-blockmaxsize" minus "-blockprioritysize") are taken.
        The inclusion of a transaction (target transaction) by priority is determined firstly by identifying the hash of the last
        transaction that would be included by priority in the selection; then it is checked if the target transaction actually appears
        before (in this case the target transaction is included by priority) or after (in this case the target transaction is not
        included by priority, but by fee) the just identified transaction or even if it does not appear at all (in this case the target
        transaction is not included at all, hence it is not included by priority).
        Two rounds of check are performed, in the first one the inclusion by priority is tested, in the second one the inclusion by fee
        is tested; at the end of each round an additional check is perfomed for testing that prioritization of a transaction on a node
        not involved in mining has no effect on the node actually involved in mining
        '''

        # tx priority is calculated: priority = sum(input_value_in_base_units * input_age)/size_in_bytes

        print("Mining 11kb blocks...")
        self.nodes[0].generate(501)
        self.sync_all()

        # 11 kb blocks will only hold about 50 txs, so this will fill mempool with older txs
        taddr = self.nodes[1].getnewaddress()
        for _ in range(900):
            self.nodes[0].sendtoaddress(taddr, 0.1)
        self.sync_all()

        self.nodes[0].generate(1)
        self.sync_all()

        # first round prioritization (on node0) by priority, second round prioritization (on node0) by fee
        for round in range(2):
            if (round == 0):
                print("Round with prioritization (on node0) by priority")
            elif (round == 1):
                print("Round with prioritization (on node0) by fee")
    
            # Create tx of lower value to be prioritized on node 0
            # Older transactions get mined first, so this lower value, newer tx is unlikely to be mined without prioritisation
            priority_tx_0 = self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 0.1)
            self.sync_all()

            # Keep track of which will be the last tx to mine by priority
            rawmempool = self.nodes[0].getrawmempool(True)
            last_tx_to_include_by_priority_0 = self.get_last_tx_selectable_by_priority(rawmempool)
            assert_equal(last_tx_to_include_by_priority_0 != None, True)

            # Check that priority_tx_0 was not included by priority through getblocktemplate()
            block_template = self.nodes[0].getblocktemplate()
            tx_0_included_by_priority, tx_0_included = self.check_if_included_by_priority(priority_tx_0, last_tx_to_include_by_priority_0, block_template)
            assert_equal(tx_0_included_by_priority, False)

            max_priority, max_fee = self.get_max_priority_and_max_fee(rawmempool)
            if (round == 0):
                priority_success = self.nodes[0].prioritisetransaction(priority_tx_0, max_priority, 0)
            elif (round == 1):
                priority_success = self.nodes[0].prioritisetransaction(priority_tx_0, 0, int(max_fee * COIN))
            assert_equal(priority_success, True)

            # Check that priority_tx_0 was not included by priority through getblocktemplate()
            # (not updated because no new txns)
            block_template = self.nodes[0].getblocktemplate()
            tx_0_included_by_priority, tx_0_included = self.check_if_included_by_priority(priority_tx_0, last_tx_to_include_by_priority_0, block_template)
            assert_equal(tx_0_included_by_priority, False)

            # Sending a new transaction will make getblocktemplate refresh within 10s
            self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 0.1)
            self.sync_all()

            # Keep track of which will be the last tx to mine by priority
            last_tx_to_include_by_priority_0 = self.get_last_tx_selectable_by_priority(self.nodes[0].getrawmempool(True))
            assert_equal(last_tx_to_include_by_priority_0 != None, True)

            # Check that priority_tx_0 was not included by priority through getblocktemplate()
            # (too soon)
            block_template = self.nodes[0].getblocktemplate()
            tx_0_included_by_priority, tx_0_included = self.check_if_included_by_priority(priority_tx_0, last_tx_to_include_by_priority_0, block_template)
            assert_equal(tx_0_included_by_priority, False)

            # Check that priority_tx_0 was by priority through getblocktemplate()
            # getblocktemplate() will refresh after 1 min, or after 10 sec if new transaction is added to mempool
            # Mempool is probed every 10 seconds. We'll give getblocktemplate() a maximum of {timeout_seconds} seconds to refresh
            start = time.time()
            in_block_template = False
            while in_block_template == False:
                block_template = self.nodes[0].getblocktemplate()
                tx_0_included_by_priority, tx_0_included = self.check_if_included_by_priority(priority_tx_0, last_tx_to_include_by_priority_0, block_template)
                if (round == 0 and tx_0_included_by_priority):
                    break
                elif (round == 1 and tx_0_included):
                    break
                if time.time() - start > self.timeout_seconds:
                    raise AssertionError(f"Test timed out because prioritised transaction was not returned by getblocktemplate within {self.timeout_seconds} seconds.")
                time.sleep(1)

            if (round == 0):
                assert_equal(tx_0_included_by_priority, True)
            elif (round == 1):
                assert_equal(tx_0_included_by_priority, False)
                assert_equal(tx_0_included, True)

            # Node 1 doesn't get the next block, so this will not be mined by priority despite being prioritized on node 1
            max_priority, max_fee = self.get_max_priority_and_max_fee(self.nodes[1].getrawmempool(True))
            priority_tx_1 = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), 0.1)
            self.sync_all()
            self.nodes[1].prioritisetransaction(priority_tx_1, max_priority, 0)

            # Keep track of which will be the last tx to mine by priority
            last_tx_to_mine_by_priority_0 = self.get_last_tx_selectable_by_priority(self.nodes[0].getrawmempool(True))
            assert_equal(last_tx_to_mine_by_priority_0 != None, True)

            # Mine block on node 0 and sync
            blk_hash = self.nodes[0].generate(1)
            block = self.nodes[0].getblock(blk_hash[0])
            self.sync_all()
            assert_equal(last_tx_to_mine_by_priority_0 in block["tx"], True)

            # Check that priority_tx_0 was mined by priority
            tx_0_mined_by_priority, tx_0_mined = self.check_if_mined_by_priority(priority_tx_0, last_tx_to_mine_by_priority_0, block)
            if (round == 0):
                assert_equal(tx_0_mined_by_priority, True)
            elif (round == 1):
                assert_equal(tx_0_mined_by_priority, False)
                assert_equal(tx_0_mined, True)
            # Check that priority_tx_1 was not mined by priority
            # (can be not mined at all or can be mined by fee rate)
            tx_1_mined_by_priority, tx_1_mined = self.check_if_mined_by_priority(priority_tx_1, last_tx_to_mine_by_priority_0, block)
            assert_equal(tx_1_mined_by_priority, False)

            mempool = self.nodes[0].getrawmempool()
            assert_equal(priority_tx_0 in mempool, not tx_0_mined)
            assert_equal(priority_tx_1 in mempool, not tx_1_mined)

            # Keep track of which will be the last tx to mine by priority
            last_tx_to_mine_by_priority_1 = self.get_last_tx_selectable_by_priority(self.nodes[1].getrawmempool(True))
            assert_equal(last_tx_to_mine_by_priority_1 != None, True)

            # Mine a block on node 1 and sync
            blk_hash_1 = self.nodes[1].generate(1)
            block_1 = self.nodes[1].getblock(blk_hash_1[0])
            self.sync_all()
            assert_equal(last_tx_to_mine_by_priority_1 in block_1["tx"], True)

            if (not tx_1_mined):
                # Check to see if priority_tx_1 is now mined by priority
                mempool_1 = self.nodes[1].getrawmempool()
                assert_equal(priority_tx_1 in mempool_1, tx_1_mined)
                assert_equal(priority_tx_1 in block_1['tx'], not tx_1_mined)


if __name__ == '__main__':
    PrioritiseTransactionTest().main()
