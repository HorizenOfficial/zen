#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
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
        self.items = items

    def __getitem__(self, key):
        return self.last_val

    def __iter__(self):
        def generator():
            for key, value in self.items:
                self.last_val = value
                yield key

        return generator()

class sc_cert_customfields(BitcoinTestFramework):
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
        '''

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

        #generate wCertVk and constant
        mcTest = MCTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params('sc1')
        constant = generate_random_field_element_hex()

        # create with wrong vFieldElementConfig obj
        #-------------------------------------------------------
        amount = 12.0
        fee = 0.000025
        bad_obj = {"a":1, "b":2}

        cmdInput = {'vFieldElementConfig': bad_obj, 'toaddress': toaddress, 'amount': amount, 'fee': fee, 'wCertVk': vk}

        mark_logs("\nNode 1 create SC with wrong vFieldElementConfig obj in input", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].create_sidechain(cmdInput)
            assert_true(False);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("not an array" in errorString)

        # create with wrong vCompressedMerkleTreeConfig array contents
        #-------------------------------------------------------
        amount = 12.0
        fee = 0.000025
        bad_array = ["hello", "world"]

        cmdInput = {'vCompressedMerkleTreeConfig': bad_array, 'toaddress': toaddress, 'amount': amount, 'fee': fee, 'wCertVk': vk}

        mark_logs("\nNode 1 create SC with wrong vCompressedMerkleTreeConfig array in input", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].create_sidechain(cmdInput)
            assert_true(False);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("expected int" in errorString)

        # Node 1 create the SC using a valid vFieldElementConfig / vCompressedMerkleTreeConfig pair
        #------------------------------------------------------------------------------------------
        mark_logs("\nNode 1 create SC with valid vFieldElementConfig / vCompressedMerkleTreeConfig pair", self.nodes,DEBUG_MODE)

        wel = 5
        amount = 20.0
        fee = 0.000025
        feCfg = [22, 33]
        cmtCfg = [253, 19]

        cmdInput = {
            'withdrawalEpochLength': 5, 'amount': amount, 'fee': fee, 'wCertVk': vk, 'toaddress':"cdcd",
            'vFieldElementConfig':feCfg, 'vCompressedMerkleTreeConfig':cmtCfg
        }

        try:
            res = self.nodes[1].create_sidechain(cmdInput)
            tx =   res['txid']
            scid = res['scid']
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False);

        self.sync_all()

        mark_logs("\nVerify vFieldElementConfig / vCompressedMerkleTreeConfig are correctly set in tx", self.nodes,DEBUG_MODE)
        decoded_tx = self.nodes[1].getrawtransaction(tx, 1)
        sc_id = decoded_tx['vsc_ccout'][0]['scid']
        assert_equal(scid, sc_id)

        feCfgStr  = decoded_tx['vsc_ccout'][0]['vFieldElementConfig']
        cmtCfgStr = decoded_tx['vsc_ccout'][0]['vCompressedMerkleTreeConfig']
        assert_equal([int(x) for x in feCfgStr.split()], feCfg)
        assert_equal([int(x) for x in cmtCfgStr.split()], cmtCfg)

        mark_logs("\nNode 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("\nVerify vFieldElementConfig / vCompressedMerkleTreeConfig are correctly set in scinfo", self.nodes,DEBUG_MODE)
        feCfgStr  = self.nodes[0].getscinfo(sc_id)['items'][0]['vFieldElementConfig']
        cmtCfgStr = self.nodes[0].getscinfo(sc_id)['items'][0]['vCompressedMerkleTreeConfig']
        assert_equal([int(x) for x in feCfgStr.split()], feCfg)
        assert_equal([int(x) for x in cmtCfgStr.split()], cmtCfg)


if __name__ == '__main__':
    sc_cert_customfields().main()
