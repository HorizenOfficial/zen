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

        # side chain id
        scid = "22"

        mark_logs("Node 1 generates 220 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(220)
        self.sync_all()

        creation_amount = Decimal("1.0")
        fwt_amount_1 = Decimal("2.0")
        fwt_amount_2 = Decimal("2.0")
        fwt_amount_3 = Decimal("3.0")
        fwt_amount_many = fwt_amount_1 + fwt_amount_2 + fwt_amount_3

        # ---------------------------------------------------------------------------------------
        # Node 2 try create a SC with insufficient funds
        mark_logs("\nNode 2 try creates a SC with insufficient funds", self.nodes, DEBUG_MODE)

        amounts = [{"address": "dada", "amount": creation_amount}]
        errorString = ""
        try:
            self.nodes[2].sc_create(scid, 123, "dada", creation_amount, "abcdef")
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
            self.nodes[2].sc_create(scid, 123, "dada", creation_amount, "abcdef")
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("insufficient funds" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with non hex id
        mark_logs("\nNode 1 try creates a SC with non hex id", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create("azn", 123, "dada", creation_amount, "abcdef")
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("Invalid scid format" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with null address
        mark_logs("\nNode 1 try creates a SC with null address", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create("23", 123, "", creation_amount, "abcdef")
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with null amount
        mark_logs("\nNode 1 try creates a SC with null amount", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create("24", 123, "ada", "", "abcdef")
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("Invalid amount" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with 0 amount
        mark_logs("\nNode 1 try creates a SC with 0 amount", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create("24", 123, "ada", Decimal("0.0"), "abcdef")
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("amount must be positive" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with a bad custom data
        mark_logs("\nNode 1 try creates a SC with a bad custom data", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create("24", 123, "ada", 0.1, "ciao")
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("customData format: not an hex" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with a bad custom data
        mark_logs("\nNode 1 try creates a SC with a odd number of char in custom data string", self.nodes, DEBUG_MODE)

        try:
            self.nodes[1].sc_create("24", 123, "ada", 0.1, "eaf")
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("must be even" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with a custom data too long
        mark_logs("\nNode 1 try creates a SC with too long a custom data byte string", self.nodes, DEBUG_MODE)

        cdlong = "a"*2050

        try:
            self.nodes[1].sc_create("24", 123, "ada", 0.1, cdlong)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("Invalid customData length" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with negative epocLength
        mark_logs("\nNode 1 try creates a SC with 0 epocLength", self.nodes, DEBUG_MODE)

        try:
            txbad = self.nodes[1].sc_create("24", -1, "ada", Decimal("1.0"), "101010101010")
            print self.nodes[1].getrawtransaction(txbad, 1)['vsc_ccout']
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        # ---------------------------------------------------------------------------------------
        # Node 1 create the SC
        mark_logs("\nNode 1 creates SC", self.nodes, DEBUG_MODE)

        cdField = "ccababababdd"
        self.nodes[1].sc_create(scid, 123, "dada", creation_amount, cdField)
        self.sync_all()

        mark_logs("\n...Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Verify all nodes see the new SC...", self.nodes, DEBUG_MODE)
        scinfo0 = self.nodes[0].getscinfo(scid)
        scinfo1 = self.nodes[1].getscinfo(scid)
        scinfo2 = self.nodes[2].getscinfo(scid)
        assert_equal(scinfo0, scinfo1)
        assert_equal(scinfo0, scinfo2)

        mark_logs("Verify custom data are set as expected...", self.nodes, DEBUG_MODE)
        assert_equal(scinfo0['customData'], cdField)
        mark_logs(str(scinfo0), self.nodes, DEBUG_MODE)
        mark_logs(str(scinfo1), self.nodes, DEBUG_MODE)
        mark_logs(str(scinfo2), self.nodes, DEBUG_MODE)

        # ---------------------------------------------------------------------------------------
        # Node 2 try create the SC with same id
        mark_logs("\nNode 2 try create SC with same id", self.nodes, DEBUG_MODE)

        try:
            self.nodes[2].sc_create(scid, 123, "dada", creation_amount, "abababababab")
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("scid already created" in errorString, True)

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
