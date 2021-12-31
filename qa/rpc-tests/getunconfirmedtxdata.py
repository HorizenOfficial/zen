#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, stop_nodes, wait_bitcoinds, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs
import os
import pprint
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 2

NUMB_OF_TX_A = 5
QUOTA_A = Decimal('0.1234')

NUMB_OF_TX_B = 7
QUOTA_B = Decimal('0.5678')


class getunconfirmedtxdata(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False, bool_flag=True):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert',
            '-spendzeroconfchange=%d'%bool_flag]] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def send_unconf_to_node1(self, taddr, quota, numbtx):
        tot_amount = 0
        for i in range(1, numbtx+1):
            amount = i*quota
            tot_amount += amount
            tx = self.nodes[1].sendtoaddress(taddr, amount)
            mark_logs("Node 1 sent {} coins to Node0 address {} via tx {}.".format(amount, taddr, tx), self.nodes, DEBUG_MODE)
        return tot_amount

    def run_test(self):

        taddr_0_a = self.nodes[0].getnewaddress()
        taddr_0_b = self.nodes[0].getnewaddress()

        mark_logs("Node 1 generates 101 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(101)
        self.sync_all()

        # these two will be confirmed and must not take part in the final results
        self.nodes[1].sendtoaddress(taddr_0_a, QUOTA_A)
        self.sync_all()
        self.nodes[1].sendtoaddress(taddr_0_b, QUOTA_A)
        self.sync_all()

        mark_logs("Node 1 generates 3 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(3)
        self.sync_all()

        bal_0 = self.nodes[0].getbalance()
        mark_logs("Node0 balance before: {}".format(bal_0), self.nodes, DEBUG_MODE)

        tot_amount_a = self.send_unconf_to_node1(taddr_0_a, QUOTA_A, NUMB_OF_TX_A)
        self.sync_all()

        tot_amount_b = self.send_unconf_to_node1(taddr_0_b, QUOTA_B, NUMB_OF_TX_B)
        self.sync_all()

        unconf_tot_bal = self.nodes[0].getunconfirmedbalance()
        mark_logs("Node0 unconfirmed balance: {}".format(unconf_tot_bal), self.nodes, DEBUG_MODE)

        # verify that both addresses has expected unconfirmed data
        unconf_data_0_a = self.nodes[0].getunconfirmedtxdata(taddr_0_a)
        mark_logs("Node0 unconfirmed data for address {} : {}".format(taddr_0_a, unconf_data_0_a), self.nodes, DEBUG_MODE)
        assert_equal(tot_amount_a, unconf_data_0_a['unconfirmedOutput'])
        assert_equal(NUMB_OF_TX_A, unconf_data_0_a['unconfirmedTxApperances'])

        unconf_data_0_b = self.nodes[0].getunconfirmedtxdata(taddr_0_b)
        mark_logs("Node0 unconfirmed data for address {} : {}".format(taddr_0_b, unconf_data_0_b), self.nodes, DEBUG_MODE)
        assert_equal(tot_amount_b, unconf_data_0_b['unconfirmedOutput'])
        assert_equal(NUMB_OF_TX_B, unconf_data_0_b['unconfirmedTxApperances'])

        # verify that the global unconfirmend balance is the sum of the two
        assert_equal(tot_amount_b + tot_amount_a, unconf_tot_bal)

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        unconf_tot_bal = self.nodes[0].getunconfirmedbalance()

        # verify that the global unconfirmend balance is now null
        assert_equal(0, unconf_tot_bal)

        bal_0_now = self.nodes[0].getbalance()
        mark_logs("Node0 balance now: {}".format(bal_0_now), self.nodes, DEBUG_MODE)

        # verify that the global balance is now the sum of previous unconformed and confirmed
        assert_equal(bal_0_now, bal_0 + tot_amount_b + tot_amount_a)

        #-------------------------------------------------------
        mark_logs("stopping and restarting nodes with -spendzeroconfchange=0", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False, False)

        taddr = self.nodes[0].getnewaddress()
        amount = bal_0_now / 2
        tx = self.nodes[0].sendmany("", {taddr : amount})
        mark_logs("Node 0 sent {} coins to itself at address {} via tx {}.".format(amount, taddr, tx), self.nodes, DEBUG_MODE)
        self.sync_all()
        
        tx_obj = self.nodes[0].getrawtransaction(tx, 1)

        # the tx in mempool will have two txout at twodifferent addresses, one of them is the change
        vout = tx_obj['vout']
        add_0 = vout[0]['scriptPubKey']['addresses'][0]
        val_0 = vout[0]['value']
        add_1 = vout[1]['scriptPubKey']['addresses'][0]
        val_1 = vout[1]['value']

        # verify that both addresses has expected unconfirmed data
        unconf_data_0 = self.nodes[0].getunconfirmedtxdata(add_0)
        unconf_data_1 = self.nodes[0].getunconfirmedtxdata(add_1)
        mark_logs("Node0 unconfirmed data for address {} : {}".format(add_0, unconf_data_0), self.nodes, DEBUG_MODE)
        mark_logs("Node0 unconfirmed data for address {} : {}".format(add_1, unconf_data_1), self.nodes, DEBUG_MODE)

        assert_equal(val_0, unconf_data_0['unconfirmedOutput'])
        assert_equal(1, unconf_data_0['unconfirmedTxApperances'])
        assert_equal(val_1, unconf_data_1['unconfirmedOutput'])
        assert_equal(1, unconf_data_1['unconfirmedTxApperances'])

        unconf_tot_bal = self.nodes[0].getunconfirmedbalance()
        mark_logs("Node0 unconfirmed balance: {}".format(unconf_tot_bal), self.nodes, DEBUG_MODE)

        # verify that the global unconfirmend balance is the sum of the two contributions
        assert_equal(val_0 + val_1, unconf_tot_bal)
        self.sync_all()

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        #----------------------------------------------------------------------------------
        mark_logs("stopping and restarting nodes with -spendzeroconfchange=1 (default)", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False, True)

        bal_0_now = self.nodes[0].getbalance()
        taddr = self.nodes[0].getnewaddress()
        amount = bal_0_now / 3
        tx = self.nodes[0].sendmany("", {taddr : amount})
        mark_logs("Node 0 sent {} coins to itself at address {} via tx {}.".format(amount, taddr, tx), self.nodes, DEBUG_MODE)
        self.sync_all()
        
        tx_obj = self.nodes[0].getrawtransaction(tx, 1)

        # the tx in mempool will have two txout at twodifferent addresses, one of them is the change
        vout = tx_obj['vout']
        add_0 = vout[0]['scriptPubKey']['addresses'][0]
        val_0 = vout[0]['value']
        add_1 = vout[1]['scriptPubKey']['addresses'][0]
        val_1 = vout[1]['value']

        # verify that both addresses has expected unconfirmed data
        unconf_data_0 = self.nodes[0].getunconfirmedtxdata(add_0, False, False)
        unconf_data_1 = self.nodes[0].getunconfirmedtxdata(add_1, False, False)
        mark_logs("Node0 unconfirmed data for address {} : {}".format(add_0, unconf_data_0), self.nodes, DEBUG_MODE)
        mark_logs("Node0 unconfirmed data for address {} : {}".format(add_1, unconf_data_1), self.nodes, DEBUG_MODE)

        assert_equal(val_0, unconf_data_0['unconfirmedOutput'])
        assert_equal(1, unconf_data_0['unconfirmedTxApperances'])
        assert_equal(val_1, unconf_data_1['unconfirmedOutput'])
        assert_equal(1, unconf_data_1['unconfirmedTxApperances'])

        # verify that both addresses has expected unconfirmed data if called with flag true
        unconf_data_0 = self.nodes[0].getunconfirmedtxdata(add_0, True)
        unconf_data_1 = self.nodes[0].getunconfirmedtxdata(add_1, True)
        mark_logs("Node0 unconfirmed data for address {} : {}".format(add_0, unconf_data_0), self.nodes, DEBUG_MODE)
        mark_logs("Node0 unconfirmed data for address {} : {}".format(add_1, unconf_data_1), self.nodes, DEBUG_MODE)

        assert_equal(0, unconf_data_0['unconfirmedOutput'])
        assert_equal(0, unconf_data_0['unconfirmedTxApperances'])
        assert_equal(0, unconf_data_1['unconfirmedOutput'])
        assert_equal(0, unconf_data_1['unconfirmedTxApperances'])

        # verify that both addresses has expected unconfirmed data if called without flag
        unconf_data_0 = self.nodes[0].getunconfirmedtxdata(add_0)
        unconf_data_1 = self.nodes[0].getunconfirmedtxdata(add_1)
        mark_logs("Node0 unconfirmed data for address {} : {}".format(add_0, unconf_data_0), self.nodes, DEBUG_MODE)
        mark_logs("Node0 unconfirmed data for address {} : {}".format(add_1, unconf_data_1), self.nodes, DEBUG_MODE)

        assert_equal(0, unconf_data_0['unconfirmedOutput'])
        assert_equal(0, unconf_data_0['unconfirmedTxApperances'])
        assert_equal(0, unconf_data_1['unconfirmedOutput'])
        assert_equal(0, unconf_data_1['unconfirmedTxApperances'])

        unconf_tot_bal = self.nodes[0].getunconfirmedbalance()
        mark_logs("Node0 unconfirmed balance: {}".format(unconf_tot_bal), self.nodes, DEBUG_MODE)

        # verify that the global unconfirmend balance is the sum of the two contributions
        assert_equal(0, unconf_tot_bal)

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()


if __name__ == '__main__':
    getunconfirmedtxdata().main()
