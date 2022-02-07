#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import MINIMAL_SC_HEIGHT, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    stop_nodes, wait_bitcoinds, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
    dump_sc_info_record
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal
import pprint

NUMB_OF_NODES = 3
DEBUG_MODE = 1
SC_COINS_MAT = 2
SC_VK_SIZE = 1024


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
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:NUMB_OF_NODES])
            sync_mempools(self.nodes[1:NUMB_OF_NODES])

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        This test try creating a SC with sc_create using invalid parameters and valid parameters.
        It also checks the coin mature time of the FT. For SC creation an amount of 1 ZAT is used.
        '''
        # network topology: (0)--(1)--(2)

        mark_logs("Node 1 generates {} block".format(MINIMAL_SC_HEIGHT), self.nodes, DEBUG_MODE)
        self.nodes[1].generate(MINIMAL_SC_HEIGHT)
        self.sync_all()

        creation_amount = Decimal("0.000015")
        fwt_amount_1 = Decimal("2.0")
        fwt_amount_2 = Decimal("2.0")
        fwt_amount_3 = Decimal("3.0")
        fwt_amount_many = fwt_amount_1 + fwt_amount_2 + fwt_amount_3

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()

        # ---------------------------------------------------------------------------------------
        # Node 2 try creating a SC with insufficient funds
        mark_logs("\nNode 2 try creating a SC with insufficient funds", self.nodes, DEBUG_MODE)

        amounts = [{"address": "dada", "amount": creation_amount}]
        errorString = ""
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "dada",
            'amount': Decimal("1.0"),
            'wCertVk': vk,
            'constant': constant
        }

        try:
            self.nodes[2].sc_create(cmdInput)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal(True, "Insufficient transparent funds" in errorString)

        # ---------------------------------------------------------------------------------------
        # Node 2 try creating a SC with immature funds
        mark_logs("\nNode 2 try creating a SC with immature funds", self.nodes, DEBUG_MODE)

        self.nodes[2].generate(1)
        self.sync_all()
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "dada",
            'amount': Decimal("1.0"),
            'wCertVk': vk,
            'constant': constant
        }

        try:
            self.nodes[2].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal(True, "Insufficient transparent funds" in errorString)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with null address
        mark_logs("\nNode 1 try creating a SC with null address", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "",
            'amount': creation_amount,
            'wCertVk': vk,
            'constant': constant
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with null amount
        mark_logs("\nNode 1 try creating a SC with null amount", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': "",
            'wCertVk': vk,
            'constant': constant
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("Invalid amount" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with 0 amount
        mark_logs("\nNode 1 try creating a SC with 0 amount", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': Decimal("0.0"),
            'wCertVk': vk,
            'constant': constant
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("amount can not be null" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with negative amount
        mark_logs("\nNode 1 try creating a SC with 0 amount", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': Decimal("-1.0"),
            'wCertVk': vk,
            'constant': constant
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("Amount out of range" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with a bad wCertVk
        mark_logs("\nNode 1 try creating a SC with a non hex wCertVk", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': 0.1,
            'wCertVk': "zz" * SC_VK_SIZE,
            'constant': constant
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("wCertVk: Invalid format: not an hex" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with a bad wCertVk
        mark_logs("\nNode 1 try creating a SC with a odd number of char in wCertVk", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': 0.1,
            'wCertVk': "a" * (SC_VK_SIZE - 1)
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("must be even" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with a wCertVk too short
        mark_logs("\nNode 1 try creating a SC with too short wCertVk byte string", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': 0.1,
            'wCertVk': "aa" * (SC_VK_SIZE - 1)
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("Invalid wCertVk" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with a wCertVk too long
        mark_logs("\nNode 1 try creating a SC with too long wCertVk byte string", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': 0.1,
            'wCertVk': "aa" * (SC_VK_SIZE + 1)
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("Invalid wCertVk" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with an invalid wCertVk
        mark_logs("\nNode 1 try creating a SC with an invalid wCertVk", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': 0.1,
            'wCertVk': "aa" * SC_VK_SIZE
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("Invalid wCertVk" in errorString, True)

        # ---------------------------------------------------------------------------------------

        # Node 1 try creating a SC with a bad customData
        mark_logs("\nNode 1 try creating a SC with a bad customData", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': 0.1,
            'wCertVk' : vk,
            'customData': "zz" * 1024
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("customData: Invalid format: not an hex" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with a bad customData
        mark_logs("\nNode 1 try creating a SC with a odd number of char in customData", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': 0.1,
            'wCertVk' : vk,
            'customData': "b" * 1023
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("must be even" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with customData too long
        mark_logs("\nNode 1 try creating a SC with too long customData byte string", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': 0.1,
            'wCertVk': vk,
            'customData': "bb" * 1025
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("bytes" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with a bad constant
        mark_logs("\nNode 1 try creating a SC with a non hex constant", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': 0.1,
            'wCertVk': vk,
            'constant': "zz" * SC_FIELD_SIZE
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("constant: Invalid format: not an hex" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with a bad constant
        mark_logs("\nNode 1 try creating a SC with a odd number of char in constant", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': 0.1,
            'wCertVk': vk,
            'constant': "b" * (SC_FIELD_SIZE - 1)
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("must be even" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with a constant too short
        mark_logs("\nNode 1 try creating a SC with too short constant byte string", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': 0.1,
            'wCertVk': vk,
            'constant': "bb" * (SC_FIELD_SIZE - 1)
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("bytes" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with a constant too long
        mark_logs("\nNode 1 try creating a SC with too long constant byte string", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': 0.1,
            'wCertVk': vk,
            'constant': "bb" * (SC_FIELD_SIZE + 1)
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("bytes" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try creating a SC with a bad constant
        mark_logs("\nNode 1 try creating a SC with an invalid constant", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "ada",
            'amount': 0.1,
            'wCertVk': vk,
            'constant': "aa" * SC_FIELD_SIZE
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("invalid constant" in errorString, True)

        # ---------------------------------------------------------------------------------------
        
        # Node 1 try creating a SC with negative epocLength
        mark_logs("\nNode 1 try creating a SC with 0 epochLength", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 0,
            'toaddress': "ada",
            'amount': 0.1,
            'wCertVk': vk,
            'constant': "aa" * SC_FIELD_SIZE
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("Invalid withdrawalEpochLength" in errorString, True)

        # ---------------------------------------------------------------------------------------
        
        # Node 1 try creating a SC with too long an epocLength
        mark_logs("\nNode 1 try creating a SC with epochLength that is over the max limit", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 4033,
            'toaddress': "ada",
            'amount': Decimal("1.0"),
            'wCertVk': vk,
            'customData': "aa" * SC_FIELD_SIZE
        }

        try:
            self.nodes[1].sc_create(cmdInput)
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("Invalid withdrawalEpochLength" in errorString, True)

        # ---------------------------------------------------------------------------------------
        
        # Node 1 create the SC
        mark_logs("\nNode 1 creates SC", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': 123,
            'toaddress': "dada",
            'amount': creation_amount,
            'wCertVk': vk,
            'customData': "bb" * 1024,
            'constant': constant,
            'minconf': 0
        }

        ret = self.nodes[1].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])

        mark_logs("\n...Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Verify all nodes see the new SC...", self.nodes, DEBUG_MODE)
        scinfo0 = self.nodes[0].getscinfo(scid)['items'][0]
        scinfo1 = self.nodes[1].getscinfo(scid)['items'][0]
        scinfo2 = self.nodes[2].getscinfo(scid)['items'][0]
        assert_equal(scinfo0, scinfo1)
        assert_equal(scinfo0, scinfo2)

        mark_logs("Verify fields are set as expected...", self.nodes, DEBUG_MODE)
        assert_equal(scinfo0['wCertVk'], vk)
        assert_equal(scinfo0['customData'], "bb" * 1024)
        assert_equal(scinfo0['constant'], constant)
        pprint.pprint(scinfo0)

        # ---------------------------------------------------------------------------------------
        # Check maturity of the coins
        curh = self.nodes[2].getblockcount()
        mark_logs("\nCheck maturiy of the coins", self.nodes, DEBUG_MODE)

        dump_sc_info_record(self.nodes[2].getscinfo(scid)['items'][0], 2, DEBUG_MODE)
        mark_logs("Check that %f coins will be mature at h=%d" % (creation_amount, curh + 2), self.nodes, DEBUG_MODE)
        ia = self.nodes[2].getscinfo(scid)['items'][0]["immatureAmounts"]
        for entry in ia:
            if entry["maturityHeight"] == curh + SC_COINS_MAT:
                assert_equal(entry["amount"], creation_amount)

        # Node 1 sends 1 amount to SC
        mark_logs("\nNode 1 sends " + str(fwt_amount_1) + " coins to SC", self.nodes, DEBUG_MODE)

        mc_return_address = self.nodes[1].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount_1, "scid":scid, 'mcReturnAddress': mc_return_address}]
        self.nodes[1].sc_send(cmdInput)
        self.sync_all()

        # Node 1 sends 3 amounts to SC
        mark_logs("\nNode 1 sends 3 amounts to SC (tot: " + str(fwt_amount_many) + ")", self.nodes, DEBUG_MODE)

        mc_return_address = self.nodes[1].getnewaddress()

        amounts = []
        amounts.append({"toaddress": "add1", "amount": fwt_amount_1, "scid": scid, "mcReturnAddress": mc_return_address})
        amounts.append({"toaddress": "add2", "amount": fwt_amount_2, "scid": scid, "mcReturnAddress": mc_return_address})
        amounts.append({"toaddress": "add3", "amount": fwt_amount_3, "scid": scid, "mcReturnAddress": mc_return_address})

        # Check that mcReturnAddress was properly set.
        tx_id = self.nodes[1].sc_send(amounts)
        tx_obj = self.nodes[1].getrawtransaction(tx_id, 1)
        for out in tx_obj['vft_ccout']:
            assert_equal(mc_return_address, out["mcReturnAddress"], "FT mc return address is different.")
        self.sync_all()

        mark_logs("\n...Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # Check maturity of the coins at actual height
        curh = self.nodes[2].getblockcount()

        dump_sc_info_record(self.nodes[2].getscinfo(scid)['items'][0], 2, DEBUG_MODE)
        count = 0
        mark_logs("Check that %f coins will be mature at h=%d" % (creation_amount, curh + 1), self.nodes, DEBUG_MODE)
        mark_logs("Check that %f coins will be mature at h=%d" % (fwt_amount_many + fwt_amount_1, curh + 2), self.nodes, DEBUG_MODE)
        ia = self.nodes[2].getscinfo(scid)['items'][0]["immatureAmounts"]
        for entry in ia:
            count += 1
            if entry["maturityHeight"] == curh + SC_COINS_MAT:
                assert_equal(entry["amount"], fwt_amount_many + fwt_amount_1)
            if entry["maturityHeight"] == curh + SC_COINS_MAT - 1:
                assert_equal(entry["amount"], creation_amount)

        assert_equal(count, 2)

        # Check maturity of the coins at actual height+1
        mark_logs("\n...Node0 generating 1 block", self.nodes, DEBUG_MODE)

        self.nodes[0].generate(1)
        self.sync_all()
        curh = self.nodes[2].getblockcount()

        dump_sc_info_record(self.nodes[2].getscinfo(scid)['items'][0], 2, DEBUG_MODE)
        count = 0
        mark_logs("Check that %f coins will be mature at h=%d" % (fwt_amount_many + fwt_amount_1, curh + 1), self.nodes, DEBUG_MODE)
        ia = self.nodes[2].getscinfo(scid)['items'][0]["immatureAmounts"]
        for entry in ia:
            if entry["maturityHeight"] == curh + SC_COINS_MAT - 1:
                assert_equal(entry["amount"], fwt_amount_many + fwt_amount_1)
                count += 1

        assert_equal(count, 1)

        # Check no immature coin at actual height+2
        mark_logs("\n...Node0 generating 1 block", self.nodes, DEBUG_MODE)

        self.nodes[0].generate(1)
        self.sync_all()

        scinfo = self.nodes[0].getscinfo(scid, False, False)['items'][0]
        pprint.pprint(scinfo)

        mark_logs("Check that there are no immature coins", self.nodes, DEBUG_MODE)
        ia = self.nodes[2].getscinfo(scid)['items'][0]["immatureAmounts"]
        assert_equal(len(ia), 0)

        mark_logs("Checking blockindex persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        scgeninfo = self.nodes[2].getscgenesisinfo(scid)

        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        scgeninfoPost = self.nodes[0].getscgenesisinfo(scid)
        assert_equal(scgeninfo, scgeninfoPost)


if __name__ == '__main__':
    SCCreateTest().main()
