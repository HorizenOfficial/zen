#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, stop_nodes, get_epoch_data, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, mark_logs, \
    assert_false, assert_true, swap_bytes
from test_framework.test_framework import MINIMAL_SC_HEIGHT, MINER_REWARD_POST_H200
from test_framework.mc_test.mc_test import *
import os
import pprint
import time
from decimal import Decimal
from collections import namedtuple

DEBUG_MODE = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH = 10
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')


class sc_cert_ceasing_sg(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-scproofqueuesize=0', '-logtimemicros=1']] * NUMB_OF_NODES)

        for k in range(0, NUMB_OF_NODES-1):
            connect_nodes_bi(self.nodes, k, k+1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        ''' 
        (1) Create a SC
        (2) Advance epoch
        (3) Send cert1 to sc
        (4) Advance epoch
        (5) Send cert2 to sc
        (6) Advance epoch
        (7) reach safe guard
        (8) cross safe guard --> sc is ceased
        (9) Restart nodes
        '''

        # transfer amounts

        creation_amount = Decimal("10.0")

        addr_node1 = self.nodes[1].getnewaddress()

        bwt_amount_1 = Decimal("5.0")
        bwt_amount_2 = Decimal("3.0")

        amounts_1 = [{"address": addr_node1, "amount": bwt_amount_1}]
        amounts_2 = [{"address": addr_node1, "amount": bwt_amount_2}]


        mark_logs("Node 0 generates {} block".format(MINIMAL_SC_HEIGHT), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(MINIMAL_SC_HEIGHT)
        self.sync_all()

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        constant = generate_random_field_element_hex()

        # SCs creation
        #----------------------------------------------------------------------
        vk = mcTest.generate_params("sc1")
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": constant,
            'customData': "abcdef"
        }

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        mark_logs("Node 0 created SC spending {} coins via tx1 {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()
        scid = self.nodes[0].getrawtransaction(creating_tx, 1)['vsc_ccout'][0]['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs("==> created SC ids {}".format(scid), self.nodes, DEBUG_MODE)

        mark_logs("Node0 generates {} blocks to achieve end of withdrawal epochs".format(EPOCH_LENGTH), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(EPOCH_LENGTH)
        self.sync_all()
        print("#### chain height=", self.nodes[0].getblockcount())
        print

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        ret = self.nodes[0].getscinfo(scid, False, False)['items'][0]
        pprint.pprint(ret)
        assert_equal(ret['createdAtBlockHeight'], MINIMAL_SC_HEIGHT+1)
        assert_equal(ret['endEpochHeight'], MINIMAL_SC_HEIGHT+EPOCH_LENGTH)
        assert_equal(ret['ceasingHeight'], MINIMAL_SC_HEIGHT+EPOCH_LENGTH+EPOCH_LENGTH/5)
        assert_equal(ret['epoch'], 0)
        assert_equal(ret['scid'], scid)
        assert_equal(ret['withdrawalEpochLength'], EPOCH_LENGTH)
        print

        # Certificate epoch 0 
        #----------------------------------------------------------------------
        quality = 1
        proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node1], [bwt_amount_1])

        mark_logs("Node 0 sends a cert for scid {} with a bwd transfer of {} coins to Node1 address {}".format(scid, bwt_amount_1, addr_node1), self.nodes, DEBUG_MODE)
        try:
            cert_1 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("==> certificate is {}".format(cert_1), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)
        self.sync_all()

        mark_logs("Node0 generates {} blocks to achieve end of withdrawal epochs".format(EPOCH_LENGTH), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(EPOCH_LENGTH)
        self.sync_all()

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        ret = self.nodes[0].getscinfo(scid, False, False)['items'][0]
        print("ceasingHeight   =", ret['ceasingHeight'])
        print("endEpochHeight =", ret['endEpochHeight'])
        print("epoch number     =", ret['epoch'])
        assert_equal(ret['ceasingHeight'], MINIMAL_SC_HEIGHT+2*EPOCH_LENGTH+EPOCH_LENGTH/5) 
        assert_equal(ret['endEpochHeight'], MINIMAL_SC_HEIGHT+2*EPOCH_LENGTH)
        assert_equal(ret['epoch'], 1)
        print("#### chain height=", self.nodes[0].getblockcount())
        print


        # Certificate epoch 1 
        #----------------------------------------------------------------------
        quality = 1
        proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node1], [bwt_amount_2])

        mark_logs("Node 0 sends a cert for scid {} with a bwd transfer of {} coins to Node1 address {}".format(scid, bwt_amount_2, addr_node1), self.nodes, DEBUG_MODE)
        try:
            cert_2 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts_2, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("==> certificate is {}".format(cert_2), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)
        self.sync_all()

        mark_logs("Node0 generates {} blocks to achieve end of withdrawal epochs".format(EPOCH_LENGTH), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(EPOCH_LENGTH)
        self.sync_all()

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        ret = self.nodes[0].getscinfo(scid, False, False)['items'][0]
        print("ceasingHeight   =", ret['ceasingHeight'])
        print("endEpochHeight =", ret['endEpochHeight'])
        print("epoch number     =", ret['epoch'])
        assert_equal(ret['ceasingHeight'], MINIMAL_SC_HEIGHT+3*EPOCH_LENGTH+EPOCH_LENGTH/5) 
        assert_equal(ret['endEpochHeight'], MINIMAL_SC_HEIGHT+3*EPOCH_LENGTH)
        assert_equal(ret['epoch'], 2)
        print("#### chain height=", self.nodes[0].getblockcount())
        print
        mature_only = False
        utx_out1 = self.nodes[1].gettxout(cert_1, 1)
        utx_out2 = self.nodes[1].gettxout(cert_2, 1, True, mature_only)
        print("BWT coins:     -------------------------")
        if utx_out1:
            print("cert 1 has coins: {}, confirmations={}".format(utx_out1['value'], utx_out1['confirmations']))
        if utx_out2:
            print("cert 2 has coins: {}, confirmations={}".format(utx_out2['value'], utx_out2['confirmations']))
        winfo = self.nodes[1].getwalletinfo()
        assert_equal(bwt_amount_1, winfo['balance'])
        assert_equal(bwt_amount_2, winfo['immature_balance']) 
        assert_equal(2, winfo['txcount']) 
        print("----------------------------------------")
        print

        mark_logs("Node0 generates 1 more blocks reaching the end of the cert submission window", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        ret = self.nodes[0].getscinfo(scid, False, False)['items'][0]
        print("ceasingHeight   =", ret['ceasingHeight'])
        print("endEpochHeight =", ret['endEpochHeight'])
        print("epoch number     =", ret['epoch'])
        print("#### chain height=", self.nodes[0].getblockcount())
        print

        utx_out1 = self.nodes[1].gettxout(cert_1, 1)
        utx_out2 = self.nodes[1].gettxout(cert_2, 1)
        print("BWT coins:     -------------------------")
        if utx_out1:
            print("cert 1 has coins: {}, confirmations={}".format(utx_out1['value'], utx_out1['confirmations']))
        if utx_out2:
            print("cert 2 has coins: {}, confirmations={}".format(utx_out2['value'], utx_out2['confirmations']))
        winfo = self.nodes[1].getwalletinfo()
        assert_equal(bwt_amount_1, winfo['balance'])
        assert_equal(bwt_amount_2, winfo['immature_balance']) 
        assert_equal(2, winfo['txcount']) 
        print("----------------------------------------")
        print

        mark_logs("Node0 generates 1 more blocks to achieve scs ceasing", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        print("#### chain height=", self.nodes[0].getblockcount())
        print
        ret = self.nodes[0].getscinfo(scid, False, False)['items'][0]
        print("state  =", ret['state'])
        assert_equal(ret['state'], "CEASED")

        utx_out1 = self.nodes[1].gettxout(cert_1, 1)
        utx_out2 = self.nodes[1].gettxout(cert_2, 1)
        print("BWT coins:     -------------------------")
        if utx_out1:
            print("cert 1 has coins: {}, confirmations={}".format(utx_out1['value'], utx_out1['confirmations']))
        if utx_out2:
            assert(False)

        print("----------------------------------------")
        winfo = self.nodes[1].getwalletinfo()
        assert_equal(bwt_amount_1, winfo['balance'])
        assert_equal(Decimal("0.0"), winfo['immature_balance']) 
        assert_equal(2, winfo['txcount']) 
        print

        mark_logs("Node 0 sends an empty cert for scid {}".format(scid), self.nodes, DEBUG_MODE)
        try:
            #Create proof for WCert
            quality = 2
            proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [], [])

            cert_2 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, [], FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("==> certificate is {}".format(cert_2), self.nodes, DEBUG_MODE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
        self.sync_all()

        utx_out1 = self.nodes[1].gettxout(cert_1, 1)
        utx_out2 = self.nodes[1].gettxout(cert_2, 1)
        if not utx_out1:
            assert(False)
        if utx_out2:
            assert(False)

        winfo = self.nodes[1].getwalletinfo()
        assert_equal(bwt_amount_1, winfo['balance'])
        assert_equal(Decimal("0.0"), winfo['immature_balance']) 
        assert_equal(2, winfo['txcount']) 
        print("----------------------------------------")

        mark_logs("Checking certificates persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        ret = self.nodes[0].getscinfo(scid, False, False)['items'][0]
        print("state  =", ret['state'])
        assert_equal(ret['state'], "CEASED")

        utx_out1 = self.nodes[1].gettxout(cert_1, 1)
        utx_out2 = self.nodes[1].gettxout(cert_2, 1)
        if not utx_out1:
            assert(False)
        if utx_out2:
            assert(False)
        winfo = self.nodes[1].getwalletinfo()
        assert_equal(bwt_amount_1, winfo['balance'])
        assert_equal(Decimal("0.0"), winfo['immature_balance']) 
        assert_equal(2, winfo['txcount']) 


if __name__ == '__main__':
    sc_cert_ceasing_sg().main()
