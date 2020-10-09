#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, stop_nodes, wait_bitcoinds, sync_blocks, sync_mempools, connect_nodes_bi , \
     wait_and_assert_operationid_status
import os
import pprint
from decimal import *

DEBUG_MODE = 1
NUMB_OF_NODES = 2

NUMB_OF_TX_A = 5
QUOTA_A = Decimal('0.1234')

NUMB_OF_TX_B = 7
QUOTA_B = Decimal('0.5678')


class getunconfirmedtxdata(BitcoinTestFramework):

    alert_filename = None

    def mark_logs(self, msg):
        print msg
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)

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
            self.mark_logs("Node 1 sent {} coins to Node0 address {} via tx {}.".format(amount, taddr, tx))
        return tot_amount

    def run_test(self):

        taddr_0_a = self.nodes[0].getnewaddress()
        taddr_0_b = self.nodes[0].getnewaddress()

        self.mark_logs("Node 1 generates 101 block")
        self.nodes[1].generate(101)
        self.sync_all()

        # these two will be confirmed and must not take part in the final results
        self.nodes[1].sendtoaddress(taddr_0_a, QUOTA_A)
        self.sync_all()
        self.nodes[1].sendtoaddress(taddr_0_b, QUOTA_A)
        self.sync_all()

        self.mark_logs("Node 1 generates 3 block")
        self.nodes[1].generate(3)
        self.sync_all()

        bal_0 = self.nodes[0].getbalance()
        self.mark_logs("Node0 balance before: {}".format(bal_0))

        tot_amount_a = self.send_unconf_to_node1(taddr_0_a, QUOTA_A, NUMB_OF_TX_A)
        self.sync_all()

        tot_amount_b = self.send_unconf_to_node1(taddr_0_b, QUOTA_B, NUMB_OF_TX_B)
        self.sync_all()

        unconf_tot_bal = self.nodes[0].getunconfirmedbalance()
        self.mark_logs("Node0 unconfirmed balance: {}".format(unconf_tot_bal))

        # verify that both addresses has expected unconfirmed data
        unconf_data_0_a = self.nodes[0].getunconfirmedtxdata(taddr_0_a)
        self.mark_logs("Node0 unconfirmed data for address {} : {}".format(taddr_0_a, unconf_data_0_a))
        assert_equal(tot_amount_a, unconf_data_0_a['unconfirmedOutput'])
        assert_equal(NUMB_OF_TX_A, unconf_data_0_a['unconfirmedTxApperances'])

        unconf_data_0_b = self.nodes[0].getunconfirmedtxdata(taddr_0_b)
        self.mark_logs("Node0 unconfirmed data for address {} : {}".format(taddr_0_b, unconf_data_0_b))
        assert_equal(tot_amount_b, unconf_data_0_b['unconfirmedOutput'])
        assert_equal(NUMB_OF_TX_B, unconf_data_0_b['unconfirmedTxApperances'])

        # verify that the global unconfirmend balance is the sum of the two
        assert_equal(tot_amount_b + tot_amount_a, unconf_tot_bal)

        self.mark_logs("Node 1 generates 1 block")
        self.nodes[1].generate(1)
        self.sync_all()

        unconf_tot_bal = self.nodes[0].getunconfirmedbalance()

        # verify that the global unconfirmend balance is now null
        assert_equal(0, unconf_tot_bal)

        bal_0_now = self.nodes[0].getbalance()
        self.mark_logs("Node0 balance now: {}".format(bal_0_now))

        # verify that the global balance is now the sum of previous unconformed and confirmed
        assert_equal(bal_0_now, bal_0 + tot_amount_b + tot_amount_a)

        # verify that we have the expected number of related txes
        ret_a = self.nodes[1].listtxesbyaddress(taddr_0_a)
        assert_equal(NUMB_OF_TX_A + 1, len(ret_a))
        ret_b = self.nodes[1].listtxesbyaddress(taddr_0_b)
        assert_equal(NUMB_OF_TX_B + 1, len(ret_b))

        #-------------------------------------------------------
        self.mark_logs("stopping and restarting nodes with -spendzeroconfchange=0")
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False, False)

        taddr = self.nodes[0].getnewaddress()
        amount = Decimal(bal_0_now) * Decimal("0.5")
        tx = self.nodes[0].sendmany("", {taddr : amount})
        self.mark_logs("Node 0 sent {} coins to itself at address {} via tx {}.".format(amount, taddr, tx))
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
        self.mark_logs("Node0 unconfirmed data for address {} : {}".format(add_0, unconf_data_0))
        self.mark_logs("Node0 unconfirmed data for address {} : {}".format(add_1, unconf_data_1))

        assert_equal(val_0, unconf_data_0['unconfirmedOutput'])
        assert_equal(1, unconf_data_0['unconfirmedTxApperances'])
        assert_equal(val_1, unconf_data_1['unconfirmedOutput'])
        assert_equal(1, unconf_data_1['unconfirmedTxApperances'])

        unconf_tot_bal = self.nodes[0].getunconfirmedbalance()
        self.mark_logs("Node0 unconfirmed balance: {}".format(unconf_tot_bal))

        # verify that the global unconfirmend balance is the sum of the two contributions
        assert_equal(val_0 + val_1, unconf_tot_bal)
        self.sync_all()

        self.mark_logs("Node 1 generates 1 block")
        self.nodes[1].generate(1)
        self.sync_all()

        #----------------------------------------------------------------------------------
        self.mark_logs("stopping and restarting nodes with -spendzeroconfchange=1 (default)")
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False, True)

        bal_0_now = self.nodes[0].getbalance()
        taddr = self.nodes[0].getnewaddress()
        # rounding at 8 decimal ciphers, otherwise we can have assert_equal failures
        amount_to_me = (Decimal(bal_0_now) / Decimal("3")).quantize(Decimal('.00000001'))
        tx = self.nodes[0].sendmany("", {taddr : amount_to_me})
        self.mark_logs("Node 0 sent {} coins to itself at address {} via tx {}.".format(amount_to_me, taddr, tx))
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
        self.mark_logs("Node0 unconfirmed data for address {} : {}".format(add_0, unconf_data_0))
        self.mark_logs("Node0 unconfirmed data for address {} : {}".format(add_1, unconf_data_1))

        assert_equal(val_0, unconf_data_0['unconfirmedOutput'])
        assert_equal(1, unconf_data_0['unconfirmedTxApperances'])
        assert_equal(val_1, unconf_data_1['unconfirmedOutput'])
        assert_equal(1, unconf_data_1['unconfirmedTxApperances'])

        # verify that both addresses has expected unconfirmed data if called with flag true
        unconf_data_0 = self.nodes[0].getunconfirmedtxdata(add_0, True)
        unconf_data_1 = self.nodes[0].getunconfirmedtxdata(add_1, True)
        self.mark_logs("Node0 unconfirmed data for address {} : {}".format(add_0, unconf_data_0))
        self.mark_logs("Node0 unconfirmed data for address {} : {}".format(add_1, unconf_data_1))

        assert_equal(0, unconf_data_0['unconfirmedOutput'])
        assert_equal(0, unconf_data_0['unconfirmedTxApperances'])
        assert_equal(0, unconf_data_1['unconfirmedOutput'])
        assert_equal(0, unconf_data_1['unconfirmedTxApperances'])

        # verify that both addresses has expected unconfirmed data if called without flag
        unconf_data_0 = self.nodes[0].getunconfirmedtxdata(add_0)
        unconf_data_1 = self.nodes[0].getunconfirmedtxdata(add_1)
        self.mark_logs("Node0 unconfirmed data for address {} : {}".format(add_0, unconf_data_0))
        self.mark_logs("Node0 unconfirmed data for address {} : {}".format(add_1, unconf_data_1))

        assert_equal(0, unconf_data_0['unconfirmedOutput'])
        assert_equal(0, unconf_data_0['unconfirmedTxApperances'])
        assert_equal(0, unconf_data_1['unconfirmedOutput'])
        assert_equal(0, unconf_data_1['unconfirmedTxApperances'])

        unconf_tot_bal = self.nodes[0].getunconfirmedbalance()
        self.mark_logs("Node0 unconfirmed balance: {}".format(unconf_tot_bal))

        # verify that the global unconfirmend balance is the sum of the two contributions
        assert_equal(0, unconf_tot_bal)

        self.mark_logs("Node 1 generates 1 block")
        self.nodes[1].generate(1)
        self.sync_all()

        fee = Decimal("0.00001")
        amount_to_1 = Decimal("1.0")
        taddr_1 = self.nodes[1].getnewaddress()
        recipients= [{"address":taddr_1, "amount": amount_to_1}]
        myopid = self.nodes[0].z_sendmany(taddr, recipients, 1, fee, True)
        tx = wait_and_assert_operationid_status(self.nodes[0], myopid)
        self.sync_all()
        self.mark_logs("\n===> Node0 sent {} coins to Node1 at addr {} via tx={}".format(amount_to_1, taddr_1, tx))

        ud0f_ch = self.nodes[0].getunconfirmedtxdata(taddr, False)
        print (ud0f_ch)

        # this is the UTXO received initailly from iitself and used as input for the latest tx
        assert_equal(ud0f_ch['unconfirmedInput'], amount_to_me) 
        # this is the unconfirmed  change, that is = input - output - fee
        unconf_change = amount_to_me - amount_to_1 - fee
        assert_equal(ud0f_ch['unconfirmedOutput'], unconf_change) 

        assert_equal(ud0f_ch['unconfirmedTxApperances'], 1) 

        # verify that this last tx has the expected vin containing the utxo to be spent
        ret = self.nodes[0].listtxesbyaddress(taddr)[0]
        assert_equal(tx, ret['txid'])
        assert_equal(taddr, ret['vin'][0]['addr'])
        assert_equal(amount_to_me, ret['vin'][0]['value'])



if __name__ == '__main__':
    getunconfirmedtxdata().main()
