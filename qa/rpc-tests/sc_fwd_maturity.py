#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_false, assert_equal, initialize_chain_clean, \
    mark_logs, start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, \
    disconnect_nodes, dump_sc_info, dump_sc_info_record
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal
import time

NUMB_OF_NODES = 3
DEBUG_MODE = 1


class sc_fwd_maturity(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(
            NUMB_OF_NODES, self.options.tmpdir,
            extra_args=[['-sccoinsmaturity=2', '-logtimemicros=1', '-debug=sc',
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

    def split_network(self):
        # Split the network of three nodes into nodes 0-1 and 2.
        assert not self.is_network_split
        disconnect_nodes(self.nodes[1], 2)
        disconnect_nodes(self.nodes[2], 1)
        self.is_network_split = True

    def join_network(self):
        # Join the (previously split) network pieces together: 0-1-2
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 1)
        # self.sync_all()
        time.sleep(2)
        self.is_network_split = False


    def run_test(self):

        ''' This test creates a sidechain, forwards funds to it and then verifies
          that scinfo is updated correctly also after invalidating the chain on a node
          step by step
        '''
        # network topology: (0)--(1)--(2)
        mark_logs("Node 1 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[1].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()

        errorString = ""

        creation_amount = Decimal("1.0")
        fwt_amount_1 = Decimal("2.0")
        fwt_amount_2 = Decimal("2.0")
        fwt_amount_3 = Decimal("3.0")
        fwt_amount_many = fwt_amount_1 + fwt_amount_2 + fwt_amount_3

        # ---------------------------------------------------------------------------------------
        print("Current height: ", self.nodes[2].getblockcount())

        mark_logs("\nNode 1 creates SC 1 with " + str(creation_amount) + " coins", self.nodes, DEBUG_MODE)
        amounts = []
        amounts.append({"address": "dada", "amount": creation_amount})
        
        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)

        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()

        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": 123,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": constant
        }

        ret = self.nodes[1].sc_create(cmdInput)
        scid_1 = ret['scid']
        self.sync_all()
        mark_logs("created SC id: {}".format(scid_1), self.nodes, DEBUG_MODE)

        mark_logs("\n...Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # ----------------------------------------------------------------------------
        curh = self.nodes[2].getblockcount()
        print("Current height: ", curh)
        dump_sc_info_record(self.nodes[2].getscinfo(scid_1)['items'][0], 2)
        print("Check that %f coins will be mature at h=%d" % (creation_amount, curh + 2))
        ia = self.nodes[2].getscinfo(scid_1)['items'][0]["immatureAmounts"]
        for entry in ia:
            if entry["maturityHeight"] == curh + 2:
                assert_equal(entry["amount"], creation_amount)
            print("...OK")
            print

        # raw_input("Press enter to send...")
        mark_logs("\nNode 1 sends " + str(fwt_amount_1) + " coins to SC", self.nodes, DEBUG_MODE)
        mc_return_address = self.nodes[1].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount_1, "scid": scid_1, 'mcReturnAddress': mc_return_address}]
        self.nodes[1].sc_send(cmdInput)
        self.sync_all()

        mark_logs("\nNode 1 sends 3 amounts to SC 1 (tot: " + str(fwt_amount_many) + ")", self.nodes, DEBUG_MODE)
        mc_return_address = self.nodes[1].getnewaddress()
        amounts = []

        amounts.append({"toaddress": "add1", "amount": fwt_amount_1, "scid": scid_1, "mcReturnAddress": mc_return_address})
        amounts.append({"toaddress": "add2", "amount": fwt_amount_2, "scid": scid_1, "mcReturnAddress": mc_return_address})
        amounts.append({"toaddress": "add3", "amount": fwt_amount_3, "scid": scid_1, "mcReturnAddress": mc_return_address})
        tx = self.nodes[1].sc_send(amounts)

        self.sync_all()

        mark_logs("\nNode 1 creates SC 2,3,4, all with " + str(creation_amount) + " coins", self.nodes, DEBUG_MODE)
        amounts = []
        amounts.append({"toaddress": "dada", "amount": creation_amount})

        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": 123,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": mcTest.generate_params("sc2"),
            "constant": generate_random_field_element_hex()
        }
        
        ret = self.nodes[1].sc_create(cmdInput)
        scid_2 = ret['scid']
        mark_logs("created SC id: {}".format(scid_2), self.nodes, DEBUG_MODE)

        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": 123,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": mcTest.generate_params("sc3"),
            "constant": generate_random_field_element_hex()
        }
        ret = self.nodes[1].sc_create(cmdInput)
        scid_3 = ret['scid']
        mark_logs("created SC id: {}".format(scid_3), self.nodes, DEBUG_MODE)

        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": 123,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": mcTest.generate_params("sc4"),
            "constant": generate_random_field_element_hex()
        }
        ret = self.nodes[1].sc_create(cmdInput)
        scid_4 = ret['scid']
        mark_logs("created SC id: {}".format(scid_4), self.nodes, DEBUG_MODE)

        self.sync_all()

        mark_logs("\n...Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        tx_details = self.nodes[1].gettransaction(tx)["details"]
        assert_equal(1, len(tx_details))
        assert_equal(tx_details[0]["category"], "send")
        assert_equal(tx_details[0]["account"], "")
        assert_equal(tx_details[0]["amount"], -fwt_amount_many)

        # ----------------------------------------------------------------------------
        curh = self.nodes[2].getblockcount()
        print("Current height: ", curh)
        dump_sc_info_record(self.nodes[2].getscinfo(scid_1)['items'][0], 2)
        dump_sc_info_record(self.nodes[2].getscinfo(scid_2)['items'][0], 2)
        count = 0
        print("Check that %f coins will be mature at h=%d" % (creation_amount, curh + 1))
        print("Check that %f coins will be mature at h=%d" % (fwt_amount_many + fwt_amount_1, curh + 2))
        ia = self.nodes[2].getscinfo(scid_1)['items'][0]["immatureAmounts"]
        for entry in ia:
            count += 1
            if entry["maturityHeight"] == curh + 2:
                assert_equal(entry["amount"], fwt_amount_many + fwt_amount_1)
            if entry["maturityHeight"] == curh + 1:
                assert_equal(entry["amount"], creation_amount)

        assert_equal(count, 2)
        print("...OK")
        print

        mark_logs("\nNode 1 sends 2 amounts to SC 2 (tot: " + str(fwt_amount_2 + fwt_amount_3) + ")", self.nodes, DEBUG_MODE)
        amounts = []
        amounts.append({"toaddress": "add2", "amount": fwt_amount_2, "scid": scid_2, "mcReturnAddress": mc_return_address})
        amounts.append({"toaddress": "add3", "amount": fwt_amount_3, "scid": scid_2, "mcReturnAddress": mc_return_address})
        self.nodes[1].sc_send(amounts)
        self.sync_all()

        mark_logs("\n...Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        dump_sc_info(self.nodes, NUMB_OF_NODES)
        # ----------------------------------------------------------------------------
        curh = self.nodes[2].getblockcount()
        print("Current height: ", curh)
        dump_sc_info_record(self.nodes[2].getscinfo(scid_1)['items'][0], 2)
        count = 0
        print("Check that %f coins will be mature at h=%d" % (fwt_amount_many + fwt_amount_1, curh + 1))
        ia = self.nodes[2].getscinfo(scid_1)['items'][0]["immatureAmounts"]
        for entry in ia:
            if entry["maturityHeight"] == curh + 1:
                assert_equal(entry["amount"], fwt_amount_many + fwt_amount_1)
                count += 1

        assert_equal(count, 1)
        print("...OK")
        print

        mark_logs("\n...Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # ----------------------------------------------------------------------------
        curh = self.nodes[2].getblockcount()
        print("Current height: ", curh)
        dump_sc_info_record(self.nodes[2].getscinfo(scid_1)['items'][0], 2)
        count = 0
        print("Check that there are no immature coins")
        ia = self.nodes[2].getscinfo(scid_1)['items'][0]["immatureAmounts"]
        assert_equal(len(ia), 0)
        print("...OK")
        print

        mark_logs("\nNode 2 invalidates best block", self.nodes, DEBUG_MODE)
        try:
            self.nodes[2].invalidateblock(self.nodes[2].getbestblockhash())
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)
        time.sleep(1)

        # ----------------------------------------------------------------------------
        curh = self.nodes[2].getblockcount()
        print("Current height: ", curh)
        dump_sc_info_record(self.nodes[2].getscinfo(scid_1)['items'][0], 2)
        count = 0
        print("Check that %f coins will be mature at h=%d" % (fwt_amount_many + fwt_amount_1, curh + 1))
        ia = self.nodes[2].getscinfo(scid_1)['items'][0]["immatureAmounts"]
        for entry in ia:
            if entry["maturityHeight"] == curh + 1:
                assert_equal(entry["amount"], fwt_amount_many + fwt_amount_1)
                count += 1

        assert_equal(count, 1)
        print("...OK")
        print

        mark_logs("\nNode 2 invalidates best block", self.nodes, DEBUG_MODE)
        try:
            self.nodes[2].invalidateblock(self.nodes[2].getbestblockhash())
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)
        time.sleep(1)

        # ----------------------------------------------------------------------------
        curh = self.nodes[2].getblockcount()
        print("Current height: ", curh)
        dump_sc_info_record(self.nodes[2].getscinfo(scid_1)['items'][0], 2)
        count = 0
        print("Check that %f coins will be mature at h=%d" % (creation_amount, curh + 1))
        print("Check that %f coins will be mature at h=%d" % (fwt_amount_many + fwt_amount_1, curh + 2))
        ia = self.nodes[2].getscinfo(scid_1)['items'][0]["immatureAmounts"]
        for entry in ia:
            count += 1
            if entry["maturityHeight"] == curh + 2:
                assert_equal(entry["amount"], fwt_amount_many + fwt_amount_1)
            if entry["maturityHeight"] == curh + 1:
                assert_equal(entry["amount"], creation_amount)

        assert_equal(count, 2)
        print("...OK")
        print

        mark_logs("\nNode 2 invalidates best block", self.nodes, DEBUG_MODE)
        try:
            self.nodes[2].invalidateblock(self.nodes[2].getbestblockhash())
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)
        time.sleep(1)

        # ----------------------------------------------------------------------------
        curh = self.nodes[2].getblockcount()
        print("Current height: ", curh)
        dump_sc_info(self.nodes, NUMB_OF_NODES)
        print("Check that %f coins will be mature at h=%d" % (creation_amount, curh + 2))
        ia = self.nodes[2].getscinfo(scid_1)['items'][0]["immatureAmounts"]
        for entry in ia:
            if entry["maturityHeight"] == curh + 2:
                assert_equal(entry["amount"], creation_amount)
            print("...OK")
            print

        creating_tx = self.nodes[2].getscinfo(scid_1)['items'][0]['creatingTxHash']
        
        mark_logs("\nNode 2 invalidates best block", self.nodes, DEBUG_MODE)
        try:
            self.nodes[2].invalidateblock(self.nodes[2].getbestblockhash())
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)
        time.sleep(1)
        print("Current height: ", self.nodes[2].getblockcount())
        print("Checking that sc info on Node2 is not available in blockchain (just in mempool)...")
        scinfo = self.nodes[2].getscinfo(scid_1)['items'][0]
        assert_false('creatingTxHash' in scinfo)
        assert_true(scinfo['unconfCreatingTxHash'], creating_tx)

        print("...OK")
        print
        time.sleep(1)


if __name__ == '__main__':
    sc_fwd_maturity().main()
