#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import MINIMAL_SC_HEIGHT, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
    dump_sc_info, dump_sc_info_record
from test_framework.mc_test.mc_test import *
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
        self._items = items
    def items(self):
        return self._items

class SCCreateTest(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[["-sccoinsmaturity=%d" % SC_COINS_MAT, '-logtimemicros=1', '-debug=sc',
                                              '-debug=py', '-debug=mempool', '-debug=net',
                                              '-debug=bench']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        This test try to create a SC using the command sc_create using invalid parameters and valid parameters.
        '''
        #{"withdrawalEpochLength", "fromaddress", "toaddress", "amount", "minconf", "fee", "customData"};

        # network topology: (0)--(1)

        mark_logs("Node 1 generates 2 block",self.nodes,DEBUG_MODE)
        self.nodes[1].generate(2)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(MINIMAL_SC_HEIGHT),self.nodes,DEBUG_MODE)
        self.nodes[0].generate(MINIMAL_SC_HEIGHT)
        self.sync_all()

        tx = []
        errorString = ""
        toaddress = "abcdef"

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params('sc1')
        constant = generate_random_field_element_hex()
        amount = 12.0
        fee = 0.000025
        amount_below_dust_threshold = 0.00000001
        bad_amount = float(self.nodes[1].getbalance()) - fee - amount_below_dust_threshold

        # Set of invalid data to test sc_create parsing
        # NOTE !! Most test for syntactic validity of arguments
        # are already enforced in sc_create.py No need to repeat them here
        parserTests = [
            {
                "title"    : "Node 1 tries to create a SC with wrong from address",
                "node"     : 1,
                "expected" : "Invalid parameter \"fromaddress\": not a valid taddr",
                "input"    : {
                    "version": 0,
                    "fromaddress": "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2",
                    "toaddress": toaddress,
                    "amount": 0.1,
                    "fee": fee,
                    "wCertVk": vk
                }
            },
            {
                "title"    : "Node 1 tries to create a SC with changeaddress not belonging to bound wallet",
                "node"     : 1,
                "expected" : "Invalid parameter \"changeaddress\": not my address",
                "input"    : {
                    "version": 0,
                    "changeaddress": "zthWZsNRTykixeceqgifx18hMMLNrNCzCzj",
                    "toaddress": toaddress,
                    "amount": 0.1,
                    "fee": fee,
                    "wCertVk": vk
                }
            },
            {
                "title"    : "Node 1 tries to create a SC with an amount that prevents a change above the dust threshold",
                "node"     : 1,
                "expected" : "Insufficient transparent funds",
                "input"    : {
                    "version": 0,
                    "toaddress": toaddress,
                    "amount": bad_amount,
                    "fee": fee,
                    "wCertVk": vk
                }
            },
        ]

        for test in parserTests:
            mark_logs(test['title'], self.nodes, DEBUG_MODE)
            try:
                self.nodes[test['node']].sc_create(test['input'])
                assert_true(False) # We should not get here
            except JSONRPCException as ex:
                errorString = ex.error['message']
                mark_logs(" ... " + errorString, self.nodes, DEBUG_MODE)
                assert_true(test['expected'] in errorString)

        # Node 1 create the SC using a valid input and a fromaddress+changeaddress key/value
        #------------------------------------------------------------------------------------------
        mark_logs("\nNode 1 create SC with valid input and a fromaddress key/value", self.nodes,DEBUG_MODE)

        wel = 5
        fromaddr = []
        toaddress = "abcdef"
        amount = 20.0
        minconf = 1
        fee = 0.000025

        addr_found = False
        # select an address with an UTXO value large enough
        for groups in self.nodes[1].listaddressgroupings():
            if addr_found:
                break
            for entry in groups:
                if entry[1] >= amount:
                    fromaddr = entry[0]
                    addr_found = True
                    break
        
        # check we have an address with enough coins
        assert_true(len(fromaddr)>0)

        changeaddress = self.nodes[1].getnewaddress()

        mark_logs(("using fromaddress: %s "%fromaddr), self.nodes,DEBUG_MODE)
        cmdInput = {
            "version": 0,
            "fromaddress": fromaddr,
            "toaddress": toaddress,
            "amount": amount,
            "changeaddress":changeaddress,
            "fee": fee,
            'wCertVk': vk
        }

        try:
            res = self.nodes[1].sc_create(cmdInput)
            tx =   res['txid']
            scid = res['scid']
            pprint.pprint(res)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False);

        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(tx, 1)
        sc_id = decoded_tx['vsc_ccout'][0]['scid']
        assert_equal(scid, sc_id)

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
            chaddress = i['scriptPubKey']['addresses'][0]
            n = i['n']
            mark_logs((" change: %s" % change), self.nodes, DEBUG_MODE)

        # check we are sending change to the proper address
        mark_logs(("...check that we are sending change to the expected address"), self.nodes, DEBUG_MODE)
        assert_equal(changeaddress, chaddress) 

        # get sc amount
        sc_amount = 0.0
        for i in decoded_tx['vsc_ccout']:
            sc_amount += float(i['value'])
            mark_logs((" sc amount %s" % (sc_amount)), self.nodes, DEBUG_MODE)

        mark_logs((" fee: %s" % Decimal(str(fee))), self.nodes, DEBUG_MODE)

        totalOutAmount += float(change)
        totalOutAmount += float(sc_amount)

        # check that in=out+fee
        assert_equal(totalOutAmount, totalInAmount - fee)

        mark_logs("\nNode 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        dump_sc_info(self.nodes, NUMB_OF_NODES, sc_id, DEBUG_MODE)
        assert_equal(self.nodes[0].getscinfo(sc_id)['items'][0]['scid'], sc_id)

        amount2 = 5.0
        mark_logs(("\nNode 0 sends %s to Node 1" % amount2), self.nodes, DEBUG_MODE)
        self.nodes[0].sendtoaddress(fromaddr, amount2)
        self.sync_all()

        mark_logs("\nNode 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        MIN_CONF = 2

        # create with a minconf value which led to an error
        #---------------------------------------------------
        cmdInput = {
            'version': 0,
            'fromaddress':fromaddr,
            'toaddress': toaddress,
            'amount': 6.0,
            'minconf': MIN_CONF,
            'wCertVk': vk
        }

        mark_logs("\nNode 1 create SC with an minconf value in input which gives an error", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].sc_create(cmdInput)
            assert_true(False);
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("minconf" in errorString)

        MIN_CONF = 1

        # create with a minconf value which is ok
        #--------------------------------------------------------------------------------------
        cmdInput = {
            'version': 0,
            'fromaddress':fromaddr,
            'toaddress': toaddress,
            'amount': 6.0,
            'minconf': MIN_CONF,
            'wCertVk': vk,
            'customData': "bb" * 1024,
            'constant': constant
        }

        mark_logs("\nNode 1 create SC with an minconf value in input which is OK, with scid auto generation and valid custom data", self.nodes, DEBUG_MODE)
        try:
            res = self.nodes[1].sc_create(cmdInput)
            tx =   res['txid']
            scid = res['scid']
            pprint.pprint(res)
            self.sync_all()
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        decoded_tx = self.nodes[1].getrawtransaction(tx, 1)
        sc_id   = decoded_tx['vsc_ccout'][0]['scid']
        assert_equal(scid, sc_id)
        wCertVk = decoded_tx['vsc_ccout'][0]['wCertVk']
        constant = decoded_tx['vsc_ccout'][0]['constant']
        customData = decoded_tx['vsc_ccout'][0]['customData']

        # check we are sending change to the proper address, which is fromaddr, since we have not set a change address
        for i in decoded_tx['vout']:
            chaddress = i['scriptPubKey']['addresses'][0]
            n = i['n']

        mark_logs(("...check that we are sending change to the proper address, which is fromaddr, since we have not set a change address"), self.nodes, DEBUG_MODE)
        assert_equal(fromaddr, chaddress) 

        mark_logs("\nNode 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        scinfo0 = self.nodes[0].getscinfo(scid)['items'][0]
        mark_logs("...verify that scid and custom data are set as expected...", self.nodes, DEBUG_MODE)
        assert_equal(scinfo0['scid'], scid)
        assert_equal(scinfo0['wCertVk'], wCertVk)
        assert_equal(scinfo0['constant'], constant)
        assert_equal(scinfo0['customData'], "bb" * 1024)

        # test sending funds to sidechain with sbh command
        #--------------------------------------------------------------------------------------
        outputs = []
        tot_many = 0
        bal_t0 = self.nodes[1].getbalance()
        fee = Decimal('0.000123')
        mc_return_address = self.nodes[1].getnewaddress()
        for i in range(0, 10):
            outputs.append({'toaddress': toaddress, 'amount': Decimal("0.01")*(i+1), "scid": scid, "mcReturnAddress": mc_return_address})
            tot_many += outputs[-1]['amount']
        cmdParms = {'minconf': MIN_CONF, 'fee':fee}

        mark_logs("\nNode 1 sends funds in 10 transfers to sc", self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[1].sc_send(outputs, cmdParms)
            self.sync_all()
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        decoded_tx = self.nodes[1].getrawtransaction(tx, 1)
#        if DEBUG_MODE:
#            pprint.pprint(decoded_tx)

        mark_logs("\nNode 0 generates 2 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(2)
        self.sync_all()

        scinfo0 = self.nodes[0].getscinfo(scid)['items'][0]
        if DEBUG_MODE:
            pprint.pprint(scinfo0)

        assert_equal(tot_many, scinfo0['immatureAmounts'][0]['amount'])
        bal_t1 = self.nodes[1].getbalance()
        assert_equal(bal_t0, bal_t1 + tot_many + fee)

if __name__ == '__main__':
    SCCreateTest().main()
