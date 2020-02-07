#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
    dump_sc_info_record
import os
import pprint
from decimal import Decimal
import json

NUMB_OF_NODES = 2
DEBUG_MODE = 1
SC_COINS_MAT = 2


class FakeDict(dict):
    def __init__(self, items):
        # need to have something in the dictionary
        self['something'] = 'something'
        self.items = items

    def __getitem__(self, key):
        return self.last_val

    def __iter__(self):
        subiter = iter(self.items)
        def generator():
            for key, value in self.items:
                self.last_val = value
                yield key

        return generator()

class SCCreateTest(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[["-sccoinsmaturity=%d" % SC_COINS_MAT, '-logtimemicros=1', '-debug=sc',
                                              '-debug=py', '-debug=mempool', '-debug=net',
                                              '-debug=bench']] * NUMB_OF_NODES)

        if not split:
            # 1 and 2 are joint only if split==false
            if NUMB_OF_NODES > 2:
                connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:NUMB_OF_NODES])
            sync_mempools(self.nodes[1:NUMB_OF_NODES])

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        This test try to create a SC using the command create_sidechain using invalid parameters and valid parameters.
        '''
        #{"scid", "withdrawalEpochLength", "fromaddress", "toaddress", "amount", "minconf", "fee"};

        # network topology: (0)--(1)

        mark_logs("Node 1 generates 2 block",self.nodes,DEBUG_MODE)
        self.nodes[1].generate(2)
        self.sync_all()

        mark_logs("Node 0 generates 220 block",self.nodes,DEBUG_MODE)
        self.nodes[0].generate(220)
        self.sync_all()

        tx = []
        errorString = ""
        toaddress = "abcdef"

        # create with wrong key in input
        #------------------------------------
        amount = 12.0
        fee = 0.000025

        cmdInput = {'wrong_key': 123, 'toaddress': toaddress, 'amount': amount, 'fee': fee}

        mark_logs("\nNode 1 create SC with wrong key in input", self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[1].create_sidechain(cmdInput)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("wrong_key" in errorString)

        # create with duplicate key in input
        #------------------------------------
        cmdInput = FakeDict( [('fee', fee), ('amount', amount), ('amount', 6.0), ('toaddress', str(toaddress))])

        mark_logs("\nNode 1 create SC with duplicate key in input",self.nodes,DEBUG_MODE)
        try:
            tx = self.nodes[1].create_sidechain(cmdInput)
            print "tx=", tx
            assert_true(False);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("amount" in errorString)

        # create with a missing mandatory key in input
        #------------------------------------------------
        cmdInput = {'withdrawalEpochLength': 10, 'toaddress': toaddress, 'scid': "1234", 'fee': fee}

        mark_logs("\nNode 1 create SC with duplicate key in input",self.nodes,DEBUG_MODE)
        try:
            tx = self.nodes[1].create_sidechain(cmdInput)
            print "tx=", tx
            assert_true(False);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("amount" in errorString)

        # create with a bad value for amount
        #------------------------------------------------
        cmdInput = {'withdrawalEpochLength': 10, 'toaddress': toaddress, 'amount': -0.1, 'scid': "1234", 'fee': fee}

        mark_logs("\nNode 1 create SC with an invalid amount in input", self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[1].create_sidechain(cmdInput)
            print "tx=", tx
            assert_true(False);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("range" in errorString)

        # create with a bad withdrawal epoch length 
        #------------------------------------------------
        cmdInput = {'withdrawalEpochLength': 0, 'toaddress': toaddress, 'amount': 0.1, 'scid': "1234", 'fee': fee}

        mark_logs("\nNode 1 create SC with an invalid epoch length in input", self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[1].create_sidechain(cmdInput)
            print "tx=", tx
            assert_true(False);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("withdrawal" in errorString)

        # Node 1 create the SC using a valid input and a fromaddress key/value
        #------------------------------------------------------------------------------------------
        mark_logs("\nNode 1 create SC with valid input and a fromaddress key/value", self.nodes,DEBUG_MODE)

        sc_id = "aaa"
        wel = 5
        fromaddr = []
        toaddress = "abcdef"
        amount = 20.0
        minconf = 1
        fee = 0.000025

        # select an address with an UTXO value large enough
        for groups in self.nodes[1].listaddressgroupings():
            for entry in groups:
                if entry[1] >= amount:
                    fromaddr = entry[0]
                    break
        
        mark_logs(("using fromaddress: %s "%fromaddr), self.nodes,DEBUG_MODE)
        cmdInput = {"fromaddress": fromaddr, "toaddress": toaddress, "amount": amount, "fee": fee}
        try:
            tx = self.nodes[1].create_sidechain(cmdInput)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)

        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(tx, 1)

        totalOutAmount = 0.0
        totalInAmount  = 0.0

        # check that inputs come from the selected fromaddr
        mark_logs(("...check that inputs come from the expected address"), self.nodes, DEBUG_MODE)
        for i in decoded_tx['vin']:
            input_tx = i['txid']
            nvout = i['vout']
            decoded_in_tx = self.nodes[1].getrawtransaction(input_tx, 1)
            addr = decoded_in_tx['vout'][nvout]['scriptPubKey']['addresses'][0]
            val  = decoded_in_tx['vout'][nvout]['value']
            #print "%s, %s" % (addr, val)
            assert_equal(addr, fromaddr) 
            totalInAmount += float(val)

        mark_logs((" in amount: %s"% totalInAmount), self.nodes, DEBUG_MODE)

        # get change
        change = 0.0;
        for i in decoded_tx['vout']:
            change = i['value']
            n = i['n']
            mark_logs((" change: %s" % change), self.nodes, DEBUG_MODE)

        # get fwd amount
        fwd_amount = 0.0
        for i in decoded_tx['vft_ccout']:
            fwd_amount += float(i['value'])
            mark_logs((" fwd amount %s" % (fwd_amount)), self.nodes, DEBUG_MODE)

        mark_logs((" fee: %s" % Decimal(str(fee))), self.nodes, DEBUG_MODE)

        totalOutAmount += float(change)
        totalOutAmount += float(fwd_amount)

        # check that in=out+fee
        assert_equal(totalOutAmount, totalInAmount - fee)

        mark_logs("\nNode 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        amount2 = 5.0
        mark_logs(("\nNode 0 sends %s to Node 1" % amount2), self.nodes, DEBUG_MODE)
        self.nodes[0].sendtoaddress(fromaddr, amount2)
        self.sync_all()

        mark_logs("\nNode 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        conf = 2

        # create with a minconf value which led to an error
        #---------------------------------------------------
        cmdInput = {'fromaddress':fromaddr, 'toaddress': toaddress, 'amount': 6.0, 'minconf': conf}

        mark_logs("\nNode 1 create SC with an minconf value in input which gives an error", self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[1].create_sidechain(cmdInput)
            print "tx=", tx
            assert_true(False);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("minconf" in errorString)

        conf = 1

        # create with a minconf value which is ok
        #---------------------------------------------------
        cmdInput = {'fromaddress':fromaddr, 'toaddress': toaddress, 'amount': 6.0, 'minconf': conf}

        mark_logs("\nNode 1 create SC with an minconf value in input which is OK", self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[1].create_sidechain(cmdInput)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

if __name__ == '__main__':
    SCCreateTest().main()
