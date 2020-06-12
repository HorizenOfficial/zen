#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
    dump_sc_info_record
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal

NUMB_OF_NODES = 3
DEBUG_MODE = 1
SC_COINS_MAT = 2


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
        This test try create a SC with sc_create using invalid parameters and valid parameters.
        It also checks the coin mature time of the FT
        '''
        # network topology: (0)--(1)--(2)

        mark_logs("Node 1 generates 220 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(220)
        self.sync_all()

        creation_amount = Decimal("1.0")
        fwt_amount_1 = Decimal("2.0")
        fwt_amount_2 = Decimal("2.0")
        fwt_amount_3 = Decimal("3.0")
        fwt_amount_many = fwt_amount_1 + fwt_amount_2 + fwt_amount_3

        #generate wCertVk and constant
        vk = generate_params(self.options.tmpdir,"sc1")
        constant = generate_random_field_element_hex()

        # ---------------------------------------------------------------------------------------
        # Node 2 try create a SC with insufficient funds
        mark_logs("\nNode 2 try creates a SC with insufficient funds", self.nodes, DEBUG_MODE)

        amounts = [{"address": "dada", "amount": creation_amount}]
        errorString = ""
        try:
            self.nodes[2].sc_create(123, "dada", creation_amount, vk, "", constant)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("insufficient funds" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 2 try create a SC with immature funds
        mark_logs("\nNode 2 try creates a SC with immature funds", self.nodes, DEBUG_MODE)

        self.nodes[2].generate(1)
        self.sync_all()
        try:
            self.nodes[2].sc_create(123, "dada", creation_amount, vk, "", constant)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("insufficient funds" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with null address
        mark_logs("\nNode 1 try creates a SC with null address", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "", creation_amount, vk, "", constant)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with null amount
        mark_logs("\nNode 1 try creates a SC with null amount", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", "", vk, "", constant)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("Invalid amount" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with 0 amount
        mark_logs("\nNode 1 try creates a SC with 0 amount", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", Decimal("0.0"), vk, "", constant)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("amount must be positive" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with a bad wCertVk
        mark_logs("\nNode 1 try creates a SC with a non hex wCertVk", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", 0.1, "zz" * SC_VK_SIZE, "", constant)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("wCertVk: Invalid format: not an hex" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with a bad wCertVk
        mark_logs("\nNode 1 try creates a SC with a odd number of char in wCertVk", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", 0.1, "a" * (SC_VK_SIZE - 1))
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("must be even" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with a wCertVk too short
        mark_logs("\nNode 1 try creates a SC with too short wCertVk byte string", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", 0.1, "aa" * (SC_VK_SIZE - 1))
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("bytes" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with a wCertVk too long
        mark_logs("\nNode 1 try creates a SC with too long wCertVk byte string", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", 0.1, "aa" * (SC_VK_SIZE + 1))
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("bytes" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with an invalid wCertVk
        mark_logs("\nNode 1 try creates a SC with an invalid wCertVk", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", 0.1, "aa" * SC_VK_SIZE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("Invalid wCertVk" in errorString, True)

        # ---------------------------------------------------------------------------------------

        # Node 1 try create a SC with a bad customData
        mark_logs("\nNode 1 try creates a SC with a bad customData", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", 0.1, vk, "zz" * 1024)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("customData: Invalid format: not an hex" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with a bad customData
        mark_logs("\nNode 1 try creates a SC with a odd number of char in customData", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", 0.1, vk, "b" * 1023)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("must be even" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with customData too long
        mark_logs("\nNode 1 try creates a SC with too long customData byte string", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", 0.1, vk, "bb" * 1025)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("bytes" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with a bad constant
        mark_logs("\nNode 1 try creates a SC with a non hex constant", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", 0.1, vk, "", "zz" * SC_FIELD_SIZE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("constant: Invalid format: not an hex" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with a bad constant
        mark_logs("\nNode 1 try creates a SC with a odd number of char in constant", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", 0.1, vk, "", "b" * (SC_FIELD_SIZE - 1))
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("must be even" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with a constant too short
        mark_logs("\nNode 1 try creates a SC with too short constant byte string", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", 0.1, vk, "", "bb" * (SC_FIELD_SIZE - 1))
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("bytes" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with a constant too long
        mark_logs("\nNode 1 try creates a SC with too long constant byte string", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", 0.1, vk, "", "bb" * (SC_FIELD_SIZE + 1))
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("bytes" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with a bad constant
        mark_logs("\nNode 1 try creates a SC with an invalid constant", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create(123, "ada", 0.1, vk, "", "aa" * SC_FIELD_SIZE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("Invalid constant" in errorString, True)

        # ---------------------------------------------------------------------------------------
        
        # Node 1 try create a SC with negative epocLength
        mark_logs("\nNode 1 try creates a SC with 0 epochLength", self.nodes, DEBUG_MODE)

        try:
            txbad = self.nodes[1].sc_create(-1, "ada", Decimal("1.0"), "aa" * 1544)
            print self.nodes[1].getrawtransaction(txbad, 1)['vsc_ccout']
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        # ---------------------------------------------------------------------------------------
        # Node 1 create the SC
        mark_logs("\nNode 1 creates SC", self.nodes, DEBUG_MODE)

        tx = self.nodes[1].sc_create(123, "dada", creation_amount, vk, "bb" * 1024, constant)
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(tx, 1)
        scid = decoded_tx['vsc_ccout'][0]['scid']

        mark_logs("\n...Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Verify all nodes see the new SC...", self.nodes, DEBUG_MODE)
        scinfo0 = self.nodes[0].getscinfo(scid)
        scinfo1 = self.nodes[1].getscinfo(scid)
        scinfo2 = self.nodes[2].getscinfo(scid)
        assert_equal(scinfo0, scinfo1)
        assert_equal(scinfo0, scinfo2)

        mark_logs("Verify fields are set as expected...", self.nodes, DEBUG_MODE)
        assert_equal(scinfo0['wCertVk'], vk)
        assert_equal(scinfo0['customData'], "bb" * 1024)
        assert_equal(scinfo0['constant'], constant)
        mark_logs(str(scinfo0), self.nodes, DEBUG_MODE)
        mark_logs(str(scinfo1), self.nodes, DEBUG_MODE)
        mark_logs(str(scinfo2), self.nodes, DEBUG_MODE)

        # ---------------------------------------------------------------------------------------
        # Check maturity of the coins
        curh = self.nodes[2].getblockcount()
        mark_logs("\nCheck maturiy of the coins", self.nodes, DEBUG_MODE)

        dump_sc_info_record(self.nodes[2].getscinfo(scid), 2, DEBUG_MODE)
        mark_logs("Check that %f coins will be mature at h=%d" % (creation_amount, curh + 2), self.nodes, DEBUG_MODE)
        ia = self.nodes[2].getscinfo(scid)["immature amounts"]
        for entry in ia:
            if entry["maturityHeight"] == curh + SC_COINS_MAT:
                assert_equal(entry["amount"], creation_amount)

        # Node 1 sends 1 amount to SC
        mark_logs("\nNode 1 sends " + str(fwt_amount_1) + " coins to SC", self.nodes, DEBUG_MODE)

        self.nodes[1].sc_send("abcd", fwt_amount_1, scid)
        self.sync_all()

        # Node 1 sends 3 amounts to SC
        mark_logs("\nNode 1 sends 3 amounts to SC (tot: " + str(fwt_amount_many) + ")", self.nodes, DEBUG_MODE)

        amounts = []
        amounts.append({"address": "add1", "amount": fwt_amount_1, "scid": scid})
        amounts.append({"address": "add2", "amount": fwt_amount_2, "scid": scid})
        amounts.append({"address": "add3", "amount": fwt_amount_3, "scid": scid})
        self.nodes[1].sc_sendmany(amounts)
        self.sync_all()

        mark_logs("\n...Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # Check maturity of the coins at actual height
        curh = self.nodes[2].getblockcount()

        dump_sc_info_record(self.nodes[2].getscinfo(scid), 2, DEBUG_MODE)
        count = 0
        mark_logs("Check that %f coins will be mature at h=%d" % (creation_amount, curh + 1), self.nodes, DEBUG_MODE)
        mark_logs("Check that %f coins will be mature at h=%d" % (fwt_amount_many + fwt_amount_1, curh + 2), self.nodes, DEBUG_MODE)
        ia = self.nodes[2].getscinfo(scid)["immature amounts"]
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

        dump_sc_info_record(self.nodes[2].getscinfo(scid), 2, DEBUG_MODE)
        count = 0
        mark_logs("Check that %f coins will be mature at h=%d" % (fwt_amount_many + fwt_amount_1, curh + 1), self.nodes, DEBUG_MODE)
        ia = self.nodes[2].getscinfo(scid)["immature amounts"]
        for entry in ia:
            if entry["maturityHeight"] == curh + SC_COINS_MAT - 1:
                assert_equal(entry["amount"], fwt_amount_many + fwt_amount_1)
                count += 1

        assert_equal(count, 1)

        # Check no immature coin at actual height+2
        mark_logs("\n...Node0 generating 1 block", self.nodes, DEBUG_MODE)

        self.nodes[0].generate(1)
        self.sync_all()

        dump_sc_info_record(self.nodes[2].getscinfo(scid), 2, DEBUG_MODE)
        mark_logs("Check that there are no immature coins", self.nodes, DEBUG_MODE)
        ia = self.nodes[2].getscinfo(scid)["immature amounts"]
        assert_equal(len(ia), 0)


if __name__ == '__main__':
    SCCreateTest().main()
