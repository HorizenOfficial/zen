#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs,\
    get_epoch_data, wait_bitcoinds, stop_nodes, \
    assert_false, assert_true, swap_bytes
from test_framework.mc_test.mc_test import *
import os
import pprint
import time
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 3
EPOCH_LENGTH = 5
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')


class sc_cert_quality_wallet(BitcoinTestFramework):

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
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib', '-scproofqueuesize=0', '-logtimemicros=1' '-rescan']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        The test creates a sc, send funds to it and then sends a certificates to it,
        verifying that amount of wallet immature funds equal to backward transfer sending values,
        of certificate with highest quality, checks maturity of backward transfer and checks node
        balance after all operations.
        '''

        # forward transfer amounts
        creation_amount = Decimal("50")
        bwt_amount_1 = Decimal("1")
        bwt_amount_2 = Decimal("2")
        bwt_amount_3 = Decimal("3")
        bwt_amount_4 = Decimal("4")
        bwt_amount_5 = Decimal("5")

        addr_node2 = self.nodes[2].getnewaddress()

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()

        node1_bal = self.nodes[1].getbalance()
        mark_logs("Initial Node1 bal: {}".format(node1_bal), self.nodes, DEBUG_MODE)
        # SC creation

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": constant,
        }

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs("Node 0 created the SC {} spending {} coins via tx {}.".format(scid, creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        sc_cr_block = self.nodes[0].generate(1)[0]
        self.sync_all()
        sc_creating_height = self.nodes[0].getblockcount()
        self.nodes[0].generate(4)
        self.sync_all()
        #------------------------------------------------
        mark_logs("Epoch 0", self.nodes, DEBUG_MODE)
        #------------------------------------------------

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        amount_cert_1 = [{"address": addr_node2, "amount": bwt_amount_1}]

        #------------------------------------------------
        quality = 10
        proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node2], [bwt_amount_1])

        mark_logs("Node 1 sends cert of quality {} with bwt of {} coins for Node2 address {}".format(quality, amount_cert_1[0]["amount"], amount_cert_1[0]["address"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_0_1 = self.nodes[1].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            node1_bal = node1_bal - CERT_FEE
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()
        assert_equal(True, cert_epoch_0_1 in self.nodes[0].getrawmempool())

        mark_logs("Node0 confirms certs transfers generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        scinfo = self.nodes[2].getscinfo(scid, False, False)
        assert_equal(bwt_amount_1, creation_amount - scinfo['items'][0]['balance'])
        assert_equal(bwt_amount_1, scinfo['items'][0]['lastCertificateAmount'])
        assert_equal(cert_epoch_0_1, scinfo['items'][0]['lastCertificateHash'])
        assert_equal(quality, scinfo['items'][0]['lastCertificateQuality'])

        winfo = self.nodes[2].getwalletinfo()
        assert_equal(Decimal("0"), winfo['balance'])
        assert_equal(bwt_amount_1, winfo['immature_balance'])
        assert_equal(1, winfo['txcount'])

        #------------------------------------------------
        quality = 20
        proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node2], [bwt_amount_2])

        amount_cert_2 = [{"address": addr_node2, "amount": bwt_amount_2}]

        mark_logs("Node 0 sends cert of quality {} with bwt of {} coins for Node2 address {}".format(quality, amount_cert_2[0]["amount"], amount_cert_2[0]["address"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_0_2 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_2, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Node0 confirms certs transfers generating 4 blocks", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(4)
        self.sync_all()

        scinfo = self.nodes[2].getscinfo(scid, False, False)
        assert_equal(bwt_amount_2, creation_amount - scinfo['items'][0]['balance'])
        assert_equal(bwt_amount_2, scinfo['items'][0]['lastCertificateAmount'])
        assert_equal(cert_epoch_0_2, scinfo['items'][0]['lastCertificateHash'])
        assert_equal(quality, scinfo['items'][0]['lastCertificateQuality'])

        winfo = self.nodes[2].getwalletinfo()
        assert_equal(Decimal("0"), winfo['balance'])
        assert_equal(bwt_amount_2, winfo['immature_balance'])
        assert_equal(2, winfo['txcount'])
        #------------------------------------------------
        mark_logs("Epoch 1", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        #------------------------------------------------

        quality = 5
        proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node2], [bwt_amount_3])

        amount_cert_3 = [{"address": addr_node2, "amount": bwt_amount_3}]
        mark_logs("Node 1 sends cert of quality {} with bwt of {} coins for Node2 address".format(quality, amount_cert_3[0]["amount"], amount_cert_3[0]["address"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_1_3 = self.nodes[1].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_3, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            node1_bal = node1_bal - CERT_FEE
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        #------------------------------------------------
        quality = 12
        proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node2], [bwt_amount_3/10])

        amount_cert_3 = [{"address": addr_node2, "amount": bwt_amount_3/10}]
        mark_logs("Node 1 sends cert of quality {} with bwt of {} coins for Node2 address".format(quality, amount_cert_3[0]["amount"], amount_cert_3[0]["address"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_1_3 = self.nodes[1].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_3, FT_SC_FEE, MBTR_SC_FEE, 3*CERT_FEE)
            node1_bal = node1_bal - 3*CERT_FEE
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        #------------------------------------------------
        quality_h = 35
        proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality_h, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node2], [bwt_amount_4])

        amount_cert_4 = [{"address": addr_node2, "amount": bwt_amount_4}]
        mark_logs("Node 0 sends cert of quality {} with bwt of {} coins for Node2 address {}".format(quality_h, amount_cert_4[0]["amount"], amount_cert_4[0]["address"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_1_4 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality_h,
                epoch_cum_tree_hash, proof, amount_cert_4, FT_SC_FEE, MBTR_SC_FEE, 2*CERT_FEE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        #------------------------------------------------
        quality = 13
        proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node2], [bwt_amount_3/10])

        amount_cert_3 = [{"address": addr_node2, "amount": bwt_amount_3/10}]
        mark_logs("Node 1 sends cert of quality {} with bwt of {} coins for Node2 address {}".format(quality, amount_cert_3[0]["amount"], amount_cert_3[0]["address"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_1_3 = self.nodes[1].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_3, FT_SC_FEE, MBTR_SC_FEE, 2*CERT_FEE)
            node1_bal = node1_bal - 2*CERT_FEE
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Node0 generates 5 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()

        scinfo = self.nodes[2].getscinfo(scid, False, False)
        assert_equal(bwt_amount_4 + bwt_amount_2, creation_amount - scinfo['items'][0]['balance'])
        assert_equal(bwt_amount_4, scinfo['items'][0]['lastCertificateAmount'])
        assert_equal(cert_epoch_1_4, scinfo['items'][0]['lastCertificateHash'])
        assert_equal(quality_h, scinfo['items'][0]['lastCertificateQuality'])

        winfo = self.nodes[2].getwalletinfo()
        assert_equal(bwt_amount_2, winfo['balance'])
        assert_equal(bwt_amount_4, winfo['immature_balance'])
        assert_equal(6, winfo['txcount'])
        #------------------------------------------------
        mark_logs("Epoch 2", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        #------------------------------------------------
        quality = 25
        proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node2], [bwt_amount_5])

        amount_cert_5 = [{"address": addr_node2, "amount": bwt_amount_5}]
        mark_logs("Node 1 sends cert of quality {} with bwt of {} coins for Node2 address {}".format(quality, amount_cert_5[0]["amount"], amount_cert_5[0]["address"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_2_5 = self.nodes[1].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_5, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            node1_bal = node1_bal - CERT_FEE
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Node0 generates 5 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()

        scinfo = self.nodes[2].getscinfo(scid, False, False)
        assert_equal(bwt_amount_5 + bwt_amount_4 + bwt_amount_2, creation_amount - scinfo['items'][0]['balance'])
        assert_equal(bwt_amount_5, scinfo['items'][0]['lastCertificateAmount'])
        assert_equal(cert_epoch_2_5, scinfo['items'][0]['lastCertificateHash'])
        assert_equal(quality, scinfo['items'][0]['lastCertificateQuality'])

        winfo = self.nodes[2].getwalletinfo()
        assert_equal(bwt_amount_2 + bwt_amount_4, winfo['balance'])
        assert_equal(bwt_amount_5, winfo['immature_balance'])
        assert_equal(7, winfo['txcount'])

        h = self.nodes[1].getblockcount()
        mark_logs("Height:{} Node1 bal: {} / {}".format(h, node1_bal, self.nodes[1].getbalance()), self.nodes, DEBUG_MODE)
        assert_equal(node1_bal, self.nodes[1].getbalance())

        mark_logs("Node2 invalidate up to sc creating predecessor", self.nodes, DEBUG_MODE)
        self.nodes[2].invalidateblock(sc_cr_block)
        time.sleep(5)

        mark_logs("Node2 reconsiders block", self.nodes, DEBUG_MODE)
        self.nodes[2].reconsiderblock(sc_cr_block)
        time.sleep(5)

        scinfo = self.nodes[2].getscinfo(scid, False, False)
        assert_equal(bwt_amount_5 + bwt_amount_4 + bwt_amount_2, creation_amount - scinfo['items'][0]['balance'])
        assert_equal(bwt_amount_5, scinfo['items'][0]['lastCertificateAmount'])
        assert_equal(cert_epoch_2_5, scinfo['items'][0]['lastCertificateHash'])
        assert_equal(quality, scinfo['items'][0]['lastCertificateQuality'])

        winfo = self.nodes[2].getwalletinfo()
        assert_equal(bwt_amount_2 + bwt_amount_4, winfo['balance'])
        assert_equal(bwt_amount_5, winfo['immature_balance'])
        assert_equal(7, winfo['txcount'])

        mark_logs("Checking certificates persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        taddr1 = self.nodes[1].getnewaddress()
        amount3 = Decimal("9.0")

        mark_logs("Trying to spend an amount larger than the available funds given by top quality certs only", self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[2].sendtoaddress(taddr1, amount3)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_true("Insufficient funds" in errorString)



if __name__ == '__main__':
    sc_cert_quality_wallet().main()
