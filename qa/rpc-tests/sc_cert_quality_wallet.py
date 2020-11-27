#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs,\
    get_epoch_data, wait_bitcoinds, stop_nodes, \
    assert_false, assert_true
from test_framework.mc_test.mc_test import *
import os
import pprint
import time
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 3
EPOCH_LENGTH = 5
CERT_FEE = Decimal('0.00015')


class sc_cert_base(BitcoinTestFramework):

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
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib', '-logtimemicros=1']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        TODO add desc
        '''

        # forward transfer amounts
        creation_amount = Decimal("50")
        bwt_amount_1 = Decimal("1")
        bwt_amount_2 = Decimal("2")
        bwt_amount_3 = Decimal("3")
        bwt_amount_4 = Decimal("4")
        bwt_amount_5 = Decimal("5")

        pkh_node2 = self.nodes[2].getnewaddress("", True)

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates 220 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(220)
        self.sync_all()

        node1_bal = self.nodes[1].getbalance();
        print "Initial Node1 bal: ", node1_bal
        # SC creation

        #generate wCertVk and constant
        mcTest = MCTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()

        ret = self.nodes[0].sc_create(EPOCH_LENGTH, "dada", creation_amount, vk, "", constant)
        creating_tx = ret['txid']
        scid = ret['scid']
        mark_logs("Node 0 created the SC {} spending {} coins via tx {}.".format(scid, creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        sc_cr_block = self.nodes[0].generate(1)[0]
        self.sync_all()
        sc_creating_height = self.nodes[0].getblockcount()
        self.nodes[0].generate(4)
        self.sync_all()
        #------------------------------------------------
        print "======================================== EPOCH 0" 
        #------------------------------------------------

        epoch_block_hash, epoch_number = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_block_hash = {}".format(epoch_number, epoch_block_hash), self.nodes, DEBUG_MODE)
        prev_epoch_block_hash = self.nodes[0].getblockhash(sc_creating_height - 1 + ((epoch_number) * EPOCH_LENGTH))

        amount_cert_1 = [{"pubkeyhash": pkh_node2, "amount": bwt_amount_1}]

        #------------------------------------------------
        quality = 10
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash, quality, constant, [pkh_node2], [bwt_amount_1])

        print 
        mark_logs("Node 1 sends cert of q={} with bwt of {} coins for Node2 pkh".format(quality, amount_cert_1[0]["amount"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_0_1 = self.nodes[1].send_certificate(scid, epoch_number, quality, epoch_block_hash, proof, amount_cert_1, CERT_FEE)
            mark_logs("Certificate is {}".format(cert_epoch_0_1), self.nodes, DEBUG_MODE)
            node1_bal = node1_bal - CERT_FEE
            print
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()
        assert_equal(True, cert_epoch_0_1 in self.nodes[0].getrawmempool())

        mark_logs("Node0 confims certs transfers generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        scinfo = self.nodes[2].getscinfo(scid, False, False)
        #pprint.pprint(scinfo)
        assert_equal(bwt_amount_1, creation_amount - scinfo['items'][0]['balance']) 
        assert_equal(bwt_amount_1, scinfo['items'][0]['last certificate amount']) 
        assert_equal(cert_epoch_0_1, scinfo['items'][0]['last certificate hash']) 
        assert_equal(quality, scinfo['items'][0]['last certificate quality']) 

        winfo = self.nodes[2].getwalletinfo()
        #pprint.pprint(winfo)
        assert_equal(Decimal("0"), winfo['balance']) 
        assert_equal(bwt_amount_1, winfo['immature_balance']) 
        assert_equal(1, winfo['txcount']) 

        #------------------------------------------------
        quality = 20
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash, quality, constant, [pkh_node2], [bwt_amount_2])

        amount_cert_2 = [{"pubkeyhash": pkh_node2, "amount": bwt_amount_2}]

        print
        mark_logs("Node 0 sends cert of q={} with bwt of {} coins for Node2 pkh".format(quality, amount_cert_2[0]["amount"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_0_2 = self.nodes[0].send_certificate(scid, epoch_number, quality, epoch_block_hash, proof, amount_cert_2, CERT_FEE)
            mark_logs("Certificate is {}".format(cert_epoch_0_2), self.nodes, DEBUG_MODE)
            print
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Node0 confirms certs transfers generating 4 blocks", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(4)
        self.sync_all()

        scinfo = self.nodes[2].getscinfo(scid, False, False)
        #pprint.pprint(scinfo)
        assert_equal(bwt_amount_2, creation_amount - scinfo['items'][0]['balance']) 
        assert_equal(bwt_amount_2, scinfo['items'][0]['last certificate amount']) 
        assert_equal(cert_epoch_0_2, scinfo['items'][0]['last certificate hash']) 
        assert_equal(quality, scinfo['items'][0]['last certificate quality']) 

        winfo = self.nodes[2].getwalletinfo()
        #pprint.pprint(winfo)
        assert_equal(Decimal("0"), winfo['balance']) 
        assert_equal(bwt_amount_2, winfo['immature_balance']) 
        assert_equal(2, winfo['txcount']) 
        #------------------------------------------------
        print "======================================== EPOCH 1" 
        #------------------------------------------------
        epoch_block_hash, epoch_number = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_block_hash = {}".format(epoch_number, epoch_block_hash), self.nodes, DEBUG_MODE)
        prev_epoch_block_hash = self.nodes[0].getblockhash(sc_creating_height - 1 + ((epoch_number) * EPOCH_LENGTH))

        #------------------------------------------------
        quality = 5
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality, constant, [pkh_node2], [bwt_amount_3])

        amount_cert_3 = [{"pubkeyhash": pkh_node2, "amount": bwt_amount_3}]
        mark_logs("Node 1 sends cert of q={} with bwt of {} coins for Node2 pkh".format(quality, amount_cert_3[0]["amount"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_1_3 = self.nodes[1].send_certificate(scid, epoch_number, quality, epoch_block_hash, proof, amount_cert_3, CERT_FEE)
            mark_logs("Certificate is {}".format(cert_epoch_1_3), self.nodes, DEBUG_MODE)
            node1_bal = node1_bal - CERT_FEE
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        #------------------------------------------------
        quality = 12
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality, constant, [pkh_node2], [bwt_amount_3/10])

        amount_cert_3 = [{"pubkeyhash": pkh_node2, "amount": bwt_amount_3/10}]
        mark_logs("Node 1 sends cert of q={} with bwt of {} coins for Node2 pkh".format(quality, amount_cert_3[0]["amount"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_1_3 = self.nodes[1].send_certificate(scid, epoch_number, quality, epoch_block_hash, proof, amount_cert_3, 3*CERT_FEE)
            mark_logs("Certificate is {}".format(cert_epoch_1_3), self.nodes, DEBUG_MODE)
            node1_bal = node1_bal - 3*CERT_FEE
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        #------------------------------------------------
        quality_h = 35
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality_h, constant, [pkh_node2], [bwt_amount_4])

        amount_cert_4 = [{"pubkeyhash": pkh_node2, "amount": bwt_amount_4}]
        mark_logs("Node 0 sends cert of q={} with bwt of {} coins for Node2 pkh".format(quality_h, amount_cert_4[0]["amount"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_1_4 = self.nodes[0].send_certificate(scid, epoch_number, quality_h, epoch_block_hash, proof, amount_cert_4, 2*CERT_FEE)
            mark_logs("Certificate is {}".format(cert_epoch_1_4), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        #------------------------------------------------
        quality = 13
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality, constant, [pkh_node2], [bwt_amount_3/10])

        amount_cert_3 = [{"pubkeyhash": pkh_node2, "amount": bwt_amount_3/10}]
        mark_logs("Node 1 sends cert of q={} with bwt of {} coins for Node2 pkh".format(quality, amount_cert_3[0]["amount"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_1_3 = self.nodes[1].send_certificate(scid, epoch_number, quality, epoch_block_hash, proof, amount_cert_3, 2*CERT_FEE)
            mark_logs("Certificate is {}".format(cert_epoch_1_3), self.nodes, DEBUG_MODE)
            node1_bal = node1_bal - 2*CERT_FEE
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Node0 generates 5 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()

        scinfo = self.nodes[2].getscinfo(scid, False, False)
        #pprint.pprint(scinfo)
        assert_equal(bwt_amount_4 + bwt_amount_2, creation_amount - scinfo['items'][0]['balance']) 
        assert_equal(bwt_amount_4, scinfo['items'][0]['last certificate amount']) 
        assert_equal(cert_epoch_1_4, scinfo['items'][0]['last certificate hash']) 
        assert_equal(quality_h, scinfo['items'][0]['last certificate quality']) 

        winfo = self.nodes[2].getwalletinfo()
        #pprint.pprint(winfo)
        assert_equal(bwt_amount_2, winfo['balance']) 
        assert_equal(bwt_amount_4, winfo['immature_balance']) 
        assert_equal(6, winfo['txcount']) 
        #------------------------------------------------
        print "======================================== EPOCH 2" 
        #------------------------------------------------
        epoch_block_hash, epoch_number = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_block_hash = {}".format(epoch_number, epoch_block_hash), self.nodes, DEBUG_MODE)
        prev_epoch_block_hash = self.nodes[0].getblockhash(sc_creating_height - 1 + ((epoch_number) * EPOCH_LENGTH))
   
        #------------------------------------------------
        quality = 25
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality, constant, [pkh_node2], [bwt_amount_5])

        amount_cert_5 = [{"pubkeyhash": pkh_node2, "amount": bwt_amount_5}]
        mark_logs("Node 1 sends cert of q={} with bwt of {} coins for Node2 pkh".format(quality, amount_cert_5[0]["amount"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_2_5 = self.nodes[1].send_certificate(scid, epoch_number, quality, epoch_block_hash, proof, amount_cert_5, CERT_FEE)
            mark_logs("Certificate is {}".format(cert_epoch_2_5), self.nodes, DEBUG_MODE)
            node1_bal = node1_bal - CERT_FEE
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Node0 generates 5 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()

        scinfo = self.nodes[2].getscinfo(scid, False, False)
        #pprint.pprint(scinfo)
        assert_equal(bwt_amount_5 + bwt_amount_4 + bwt_amount_2, creation_amount - scinfo['items'][0]['balance']) 
        assert_equal(bwt_amount_5, scinfo['items'][0]['last certificate amount']) 
        assert_equal(cert_epoch_2_5, scinfo['items'][0]['last certificate hash']) 
        assert_equal(quality, scinfo['items'][0]['last certificate quality']) 

        winfo = self.nodes[2].getwalletinfo()
        #pprint.pprint(winfo)
        assert_equal(bwt_amount_2 + bwt_amount_4, winfo['balance']) 
        assert_equal(bwt_amount_5, winfo['immature_balance']) 
        assert_equal(7, winfo['txcount']) 

        utxos = self.nodes[2].listunspent(0)
        pprint.pprint(utxos)

        h = self.nodes[1].getblockcount()
        print "h={}: Node1 bal: {} / {}".format(h, node1_bal, self.nodes[1].getbalance())
        assert_equal(node1_bal, self.nodes[1].getbalance())

        mark_logs("Node2 invalidate up to sc creating predecessor", self.nodes, DEBUG_MODE)
        self.nodes[2].invalidateblock(sc_cr_block)
        time.sleep(5)

        h2 = self.nodes[2].getblockcount()
        pprint.pprint(self.nodes[2].getrawmempool())
        pprint.pprint(self.nodes[2].getwalletinfo())
        pprint.pprint(self.nodes[2].listunspent(0))

        mark_logs("Node2 reconsiders block", self.nodes, DEBUG_MODE)
        self.nodes[2].reconsiderblock(sc_cr_block)
        time.sleep(5)

        pprint.pprint(self.nodes[2].getscinfo(scid, False, False))
        pprint.pprint(self.nodes[2].getwalletinfo())
        pprint.pprint(self.nodes[2].listunspent(0))

        mark_logs("Checking certificates persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        pprint.pprint(self.nodes[2].getscinfo(scid, False, False))
        pprint.pprint(self.nodes[2].getwalletinfo())
        pprint.pprint(self.nodes[2].listunspent(0))

        taddr1 = self.nodes[1].getnewaddress()
        amount3 = Decimal("9.0")

        mark_logs("Trying to spend an amount larger than the available funds given by top quality certs only", self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[2].sendtoaddress(taddr1, amount3)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_true("Insufficient funds" in errorString)



if __name__ == '__main__':
    sc_cert_base().main()
