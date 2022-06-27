#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
    get_epoch_data, swap_bytes 
from test_framework.mc_test.mc_test import *
import os
import pprint
from decimal import Decimal
from test_framework.mininode import COIN
import json

NUMB_OF_NODES = 3
DEBUG_MODE = 1
EPOCH_LENGTH = 10
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal("0.00025")
CUSTOM_FEE_RATE_ZAT_PER_BYTE = Decimal('2.0')
CUSTOM_FEE_RATE_ZEN_PER_KBYTE = CUSTOM_FEE_RATE_ZAT_PER_BYTE/COIN*1000

class ScRpcCmdsFeeHandling(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):

        ed = "-exportdir=" + self.options.tmpdir
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
            extra_args=[
                ['-logtimemicros=1', '-debug=sc', '-debug=py', '-debug=mempool'],
                ['-logtimemicros=1', '-debug=sc', '-debug=py', '-debug=mempool',
                    '-paytxfee='+str(CUSTOM_FEE_RATE_ZEN_PER_KBYTE)],
                ['-logtimemicros=1', '-debug=sc', '-debug=py', '-debug=mempool']
                ])

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        Test the automatic fee computation for SC related transactions and certificates
        '''

        def get_fee_rate(size, fee):
            return ((fee*COIN)/size)
        
        def isclose(d1, d2, tolerance=Decimal('0.005')):
            dec1 = Decimal(d1)
            dec2 = Decimal(d2)
            return abs(dec1-dec2) <= tolerance

        mark_logs("Node 1 generates 2 block",self.nodes,DEBUG_MODE)
        self.nodes[1].generate(2)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(ForkHeights['MINIMAL_SC']),self.nodes,DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()

        # send some very small amount to node 2, which will use them as input
        mark_logs("Node 0 send small coins to Node2", self.nodes, DEBUG_MODE)
        taddr2 = self.nodes[2].getnewaddress()
        amount = 0
        NUM_OF_TX = 50
        for i in range(NUM_OF_TX):
            amount = 0.000001 
            tx_for_input_2 = self.nodes[0].sendtoaddress(taddr2, Decimal(amount))
            self.sync_all()

        mark_logs("Node 0 generates 1 block",self.nodes,DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        tx = []
        errorString = ""
        toaddress = "abcdef"

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params('sc1')
        constant = generate_random_field_element_hex()

        MIN_CONF = 1
        # create SC without specifying the fee; it is automatically computed based on fee rate set by paytxfee parameter
        #------------------------------------------------------------------------------------------------------------
        cmdInput = {
            'version': 0,
            'toaddress': toaddress,
            'amount': 6.0,
            'minconf': MIN_CONF,
            'wCertVk': vk,
            'withdrawalEpochLength': EPOCH_LENGTH,
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

        tx_fee  = self.nodes[1].getrawmempool(True)[tx]['fee']
        tx_size = self.nodes[1].getrawmempool(True)[tx]['size']
        rate = get_fee_rate(tx_size, tx_fee)
        print("tx fee={}, sz={}, feeRate={}".format(tx_fee, tx_size, rate))
        assert_true(isclose(CUSTOM_FEE_RATE_ZAT_PER_BYTE, rate))

        mark_logs("\nNode 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # test sending funds
        #--------------------------------------------------------------------------------------
        # Having a lot of very small UTXOs makes fail the automatic algorithm for min fee computation:
        #  1. the tx is composed with some UTXO and 0 fee
        #  2. the minimum fee rate 1zat/Byte is used on tx size for computing the fee
        #  3. the tx is re-composed from scratch with some more UTXO (size increases)
        #  4. --> go to 2. ... and eat up all the UTXOs
        taddr = self.nodes[0].getnewaddress()
        mark_logs("\nNode2 sends funds to node0...expect to fail", self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[2].sendtoaddress(taddr, Decimal(0.000001))
            assert_true(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)

        mark_logs("\nNode 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mc_return_address = self.nodes[2].getnewaddress()
        outputs = [{'toaddress': toaddress, 'amount': Decimal(0.000001), "scid":scid, "mcReturnAddress": mc_return_address}]
        cmdParms = {}

        # A similar failure is expected also for SC related commands if the fee is not set by the user
        mark_logs("\nNode 2 sends funds to sc... expect to fail", self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[2].sc_send(outputs, cmdParms)
            assert_true(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)

        # set the fee and resend
        fee = Decimal('0.0000009')
        cmdParms = {"fee":fee}
        mark_logs("\nNode 2 sends funds to sc", self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[2].sc_send(outputs, cmdParms)
            self.sync_all()
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        tx_fee  = self.nodes[1].getrawmempool(True)[tx]['fee']
        tx_size = self.nodes[1].getrawmempool(True)[tx]['size']
        print("tx fee={}, sz={}, feeRate={}".format(tx_fee, tx_size, get_fee_rate(tx_size, tx_fee)))
        assert_equal(fee, tx_fee)

        # set the fee to null and send another one
        cmdParms = {"fee":0.0}
        mark_logs("\nNode 2 sends funds to sc", self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[2].sc_send(outputs, cmdParms)
            self.sync_all()
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        tx_fee  = self.nodes[1].getrawmempool(True)[tx]['fee']
        tx_size = self.nodes[1].getrawmempool(True)[tx]['size']
        print("tx fee={}, sz={}, feeRate={}".format(tx_fee, tx_size, get_fee_rate(tx_size, tx_fee)))

        mark_logs("\nNode 1 sends some certificate to sc...", self.nodes, DEBUG_MODE)
        # advance epoch
        self.nodes[0].generate(EPOCH_LENGTH - 2)
        self.sync_all()
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        # send some certificate 
        scid_swapped = str(swap_bytes(scid))
        bwt_cert = []
        addr_array = []
        bwt_amount_array = []

        #==============================================================
        q = 10
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, q, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash,
            constant, addr_array, bwt_amount_array)

        # explicitly setting a fee high enough is OK
        try:
            cert = self.nodes[1].sc_send_certificate(scid, epoch_number, q,
                epoch_cum_tree_hash, proof, bwt_cert, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Send certificate failed with reason {}".format(errorString))
            assert(False)
        self.sync_all()
 
        mark_logs("cert={}".format(cert), self.nodes, DEBUG_MODE)

        cert_fee  = self.nodes[1].getrawmempool(True)[cert]['fee']
        cert_size = self.nodes[1].getrawmempool(True)[cert]['size']
        print("cert fee={}, sz={}, feeRate={}".format(cert_fee, cert_size, get_fee_rate(cert_size, cert_fee)))
        assert_equal(CERT_FEE, cert_fee)

        #==============================================================
        q = 9
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, q, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash,
            constant, addr_array, bwt_amount_array)

        # explicitly setting a fee too low is NOT OK
        try:
            cert = self.nodes[1].sc_send_certificate(scid, epoch_number, q,
                epoch_cum_tree_hash, proof, bwt_cert, FT_SC_FEE, MBTR_SC_FEE, Decimal('0.000006'))
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Send certificate failed (as expected) with reason {}".format(errorString))

        #==============================================================
        q = 11
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, q, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash,
            constant, addr_array, bwt_amount_array)

        # null fee set by user is OK
        try:
            cert = self.nodes[1].sc_send_certificate(scid, epoch_number, q,
                epoch_cum_tree_hash, proof, bwt_cert, FT_SC_FEE, MBTR_SC_FEE, Decimal("0.0"))
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Send certificate failed with reason {}".format(errorString))
            assert(False)
        self.sync_all()
 
        mark_logs("cert={}".format(cert), self.nodes, DEBUG_MODE)

        cert_fee  = self.nodes[1].getrawmempool(True)[cert]['fee']
        cert_size = self.nodes[1].getrawmempool(True)[cert]['size']
        print("cert fee={}, sz={}, feeRate={}".format(cert_fee, cert_size, get_fee_rate(cert_size, cert_fee)))
        assert_equal(Decimal('0.0'), cert_fee)


        #==============================================================
        q = 12
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, q, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash,
            constant, addr_array, bwt_amount_array)

        mark_logs("Sending certificate without specifying fee amount (aoutom comp)", self.nodes, DEBUG_MODE)
        # automatic fee computation is OK. Automatic computation takes place if the user does not specify the fee...
        try:
            cert = self.nodes[1].sc_send_certificate(scid, epoch_number, q,
                epoch_cum_tree_hash, proof, bwt_cert, FT_SC_FEE, MBTR_SC_FEE)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Send certificate failed with reason {}".format(errorString))
            assert(False)
        self.sync_all()
 
        mark_logs("cert={}".format(cert), self.nodes, DEBUG_MODE)

        cert_fee  = self.nodes[1].getrawmempool(True)[cert]['fee']
        cert_size = self.nodes[1].getrawmempool(True)[cert]['size']
        rate = get_fee_rate(cert_size, cert_fee)
        print("cert fee={}, sz={}, feeRate={}".format(cert_fee, cert_size, rate))
        assert_true(isclose(CUSTOM_FEE_RATE_ZAT_PER_BYTE, rate))

        #==============================================================
        q = 13
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, q, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash,
            constant, addr_array, bwt_amount_array)

        mark_logs("Sending certificate with a negative fee amount (aoutom comp)", self.nodes, DEBUG_MODE)
        # ...or set it as a negative value
        fee = Decimal("-1")
        try:
            cert = self.nodes[1].sc_send_certificate(scid, epoch_number, q,
                epoch_cum_tree_hash, proof, bwt_cert, FT_SC_FEE, MBTR_SC_FEE, fee)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Send certificate failed with reason {}".format(errorString))
            assert(False)
        self.sync_all()
 
        mark_logs("cert={}".format(cert), self.nodes, DEBUG_MODE)

        cert_fee  = self.nodes[1].getrawmempool(True)[cert]['fee']
        cert_size = self.nodes[1].getrawmempool(True)[cert]['size']
        rate = get_fee_rate(cert_size, cert_fee)
        print("cert fee={}, sz={}, feeRate={}".format(cert_fee, cert_size, rate))
        assert_true(isclose(CUSTOM_FEE_RATE_ZAT_PER_BYTE, rate))



if __name__ == '__main__':
    ScRpcCmdsFeeHandling().main()
