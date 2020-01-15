#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi
import os
from decimal import Decimal

NUMB_OF_NODES = 3
DEBUG_MODE = 0
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

    def dump_sc_info_record(self, info, i):
        if DEBUG_MODE == 0:
            return
        print ("  Node %d - balance: %f" % (i, info["balance"]))
        print ("    created in block: %s (%d)" % (info["created in block"], info["created at block height"]))
        print ("    created in tx:    %s" % info["creating tx hash"])
        print ("    immature amounts:  ", info["immature amounts"])

    def dump_sc_info(self, scId=""):
        if DEBUG_MODE == 0:
            return
        if scId != "":
            print ("scid: %s" % scId)
            print ("-------------------------------------------------------------------------------------")
            for i in range(0, NUMB_OF_NODES):
                try:
                    self.dump_sc_info_record(self.nodes[i].getscinfo(scId), i)
                except JSONRPCException, e:
                    print ("  Node %d: ### [no such scid: %s]" % (i, scId))
        else:
            for i in range(0, NUMB_OF_NODES):
                x = self.nodes[i].getscinfo()
                for info in x:
                    self.dump_sc_info_record(info, i)

    def mark_logs(self, msg):
        if DEBUG_MODE == 0:
            return
        print (msg)
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)
        self.nodes[2].dbg_log(msg)


    def run_test(self):
        '''
        This test try create a SC with sc_create using invalid parameters and valid parameters.
        It also checks the coin mature time of the FT
        '''
        # network topology: (0)--(1)--(2)

        # side chain id
        scid = "22"

        self.mark_logs("Node 1 generates 220 block")
        self.nodes[1].generate(220)
        self.sync_all()

        creation_amount = Decimal("1.0")
        fwt_amount_1 = Decimal("2.0")
        fwt_amount_2 = Decimal("2.0")
        fwt_amount_3 = Decimal("3.0")
        fwt_amount_many = fwt_amount_1 + fwt_amount_2 + fwt_amount_3

        # ---------------------------------------------------------------------------------------
        # Node 2 try create a SC with insufficient funds
        self.mark_logs("\nNode 2 try creates a SC with insufficient funds")

        amounts = [{"address": "dada", "amount": creation_amount}]
        errorString=""
        try:
            self.nodes[2].sc_create(scid, 123, amounts)
        except JSONRPCException, e:
            errorString = e.error['message']
            self.mark_logs(errorString)
        assert_equal("insufficient funds" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 2 try create a SC with immature funds
        self.mark_logs("\nNode 2 try creates a SC with immature funds")

        self.nodes[2].generate(1)
        self.sync_all()
        try:
            self.nodes[2].sc_create(scid, 123, amounts)
        except JSONRPCException, e:
            errorString = e.error['message']
            self.mark_logs(errorString)
        assert_equal("insufficient funds" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with non hex id
        self.mark_logs("\nNode 1 try creates a SC with non hex id")

        try:
            self.nodes[1].sc_create("azn", 123, amounts)
        except JSONRPCException, e:
            errorString = e.error['message']
            self.mark_logs(errorString)
        assert_equal("Invalid scid format" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with null address
        self.mark_logs("\nNode 1 try creates a SC with null address")

        try:
            self.nodes[1].sc_create("23", 123, [{"address": "", "amount": creation_amount}])
        except JSONRPCException, e:
            errorString = e.error['message']
            self.mark_logs(errorString)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with null amount
        self.mark_logs("\nNode 1 try creates a SC with null amount")

        try:
            self.nodes[1].sc_create("24", 123, [{"address": "ada", "amount": ""}])
        except JSONRPCException, e:
            errorString = e.error['message']
            self.mark_logs(errorString)
        assert_equal("Invalid amount" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with 0 amount
        self.mark_logs("\nNode 1 try creates a SC with 0 amount")

        try:
            self.nodes[1].sc_create("24", 123, [{"address": "ada", "amount": Decimal("0.0")}])
        except JSONRPCException, e:
            errorString = e.error['message']
            self.mark_logs(errorString)
        assert_equal("amount must be positive" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with no amount
        self.mark_logs("\nNode 1 try creates a SC with no amount")

        try:
            self.nodes[1].sc_create("24", 123, [{"address": "ada"}])
        except JSONRPCException, e:
            errorString = e.error['message']
            self.mark_logs(errorString)
        assert_equal("Amount is not a number or string" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Node 1 try create a SC with negative epocLength
        self.mark_logs("\nNode 1 try creates a SC with 0 epocLength")

        try:
            self.nodes[1].sc_create("24", -1, [{"address": "ada", "amount": Decimal("1.0")}])
        except JSONRPCException, e:
            errorString = e.error['message']
            self.mark_logs(errorString)

        # ---------------------------------------------------------------------------------------
        # Node 1 create the SC
        self.mark_logs("\nNode 1 creates SC")

        amounts = [{"address": "dada", "amount": creation_amount}]
        self.nodes[1].sc_create(scid, 123, amounts)
        self.sync_all()

        self.mark_logs("\n...Node0 generating 1 block")
        self.nodes[0].generate(1)
        self.sync_all()

        self.mark_logs("Verify all nodes see the new SC...")
        scinfo0 = self.nodes[0].getscinfo(scid)
        scinfo1 = self.nodes[1].getscinfo(scid)
        scinfo2 = self.nodes[2].getscinfo(scid)
        assert_equal(scinfo0, scinfo1)
        assert_equal(scinfo0, scinfo2)
        self.mark_logs(str(scinfo0))
        self.mark_logs(str(scinfo1))
        self.mark_logs(str(scinfo2))

        # ---------------------------------------------------------------------------------------
        # Node 2 try create the SC with same id
        self.mark_logs("\nNode 2 try create SC with same id")

        try:
            self.nodes[2].sc_create(scid, 123, amounts)
        except JSONRPCException, e:
            errorString = e.error['message']
            self.mark_logs(errorString)
        assert_equal("scid already created" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Check maturity of the coins
        curh = self.nodes[2].getblockcount()
        self.mark_logs("\nCheck maturiy of the coins")

        self.dump_sc_info_record(self.nodes[2].getscinfo(scid), 2)
        self.mark_logs ("Check that %f coins will be mature at h=%d" % (creation_amount, curh + 2))
        ia = self.nodes[2].getscinfo(scid)["immature amounts"]
        for entry in ia:
            if entry["maturityHeight"] == curh + SC_COINS_MAT:
                assert_equal(entry["amount"], creation_amount)

        # Node 1 sends 1 amount to SC
        self.mark_logs("\nNode 1 sends " + str(fwt_amount_1) + " coins to SC")

        self.nodes[1].sc_send("abcd", fwt_amount_1, scid)
        self.sync_all()

        # Node 1 sends 3 amounts to SC
        self.mark_logs("\nNode 1 sends 3 amounts to SC (tot: " + str(fwt_amount_many) + ")")

        amounts = []
        amounts.append({"address": "add1", "amount": fwt_amount_1, "scid": scid})
        amounts.append({"address": "add2", "amount": fwt_amount_2, "scid": scid})
        amounts.append({"address": "add3", "amount": fwt_amount_3, "scid": scid})
        self.nodes[1].sc_sendmany(amounts)
        self.sync_all()

        self.mark_logs("\n...Node0 generating 1 block")
        self.nodes[0].generate(1)
        self.sync_all()

        # Check maturity of the coins at actual height
        curh = self.nodes[2].getblockcount()

        self.dump_sc_info_record(self.nodes[2].getscinfo(scid), 2)
        count = 0
        self.mark_logs("Check that %f coins will be mature at h=%d" % (creation_amount, curh + 1))
        self.mark_logs ("Check that %f coins will be mature at h=%d" % (fwt_amount_many + fwt_amount_1, curh + 2))
        ia = self.nodes[2].getscinfo(scid)["immature amounts"]
        for entry in ia:
            count += 1
            if entry["maturityHeight"] == curh + SC_COINS_MAT:
                assert_equal(entry["amount"], fwt_amount_many + fwt_amount_1)
            if entry["maturityHeight"] == curh + SC_COINS_MAT - 1:
                assert_equal(entry["amount"], creation_amount)

        assert_equal(count, 2)

        # Check maturity of the coins at actual height+1
        self.mark_logs("\n...Node0 generating 1 block")

        self.nodes[0].generate(1)
        self.sync_all()
        curh = self.nodes[2].getblockcount()

        self.dump_sc_info_record(self.nodes[2].getscinfo(scid), 2)
        count = 0
        self.mark_logs("Check that %f coins will be mature at h=%d" % (fwt_amount_many + fwt_amount_1, curh + 1))
        ia = self.nodes[2].getscinfo(scid)["immature amounts"]
        for entry in ia:
            if entry["maturityHeight"] == curh + SC_COINS_MAT - 1:
                assert_equal(entry["amount"], fwt_amount_many + fwt_amount_1)
                count += 1

        assert_equal(count, 1)

        # Check no immature coin at actual height+2
        self.mark_logs("\n...Node0 generating 1 block")

        self.nodes[0].generate(1)
        self.sync_all()

        self.dump_sc_info_record(self.nodes[2].getscinfo(scid), 2)
        self.mark_logs ("Check that there are no immature coins")
        ia = self.nodes[2].getscinfo(scid)["immature amounts"]
        assert_equal(len(ia), 0)


if __name__ == '__main__':
    SCCreateTest().main()
