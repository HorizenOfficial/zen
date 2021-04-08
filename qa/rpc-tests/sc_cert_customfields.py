#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_equal, initialize_chain_clean, \
    start_nodes, stop_nodes, wait_bitcoinds, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
    dump_sc_info, dump_sc_info_record, get_epoch_data, get_spendable
from test_framework.mc_test.mc_test import *
import os
import pprint
from decimal import Decimal
import json

NUMB_OF_NODES = 2
DEBUG_MODE = 1
EPOCH_LENGTH = 5
CERT_FEE = Decimal("0.000123")

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
                                 extra_args=[['-logtimemicros=1', '-debug=sc',
                                              '-debug=py', '-debug=mempool', '-debug=net', '-debug=cert',
                                              '-debug=bench']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        Create a few SCs specifying related configurations for the custom fields that a cert must set; during
        this phase, different versions of cmd for SC creation are tested.
        Send some certificate with custom fields configured accordingly to the SC they refer.
        Along the test execution, some negative test is also performed.
        JSON representation of scinfo as well as tx/cert are checked for expected contents too.
        '''

        # network topology: (0)--(1)

        mark_logs("Node 1 generates 2 block",self.nodes,DEBUG_MODE)
        self.nodes[1].generate(2)
        self.sync_all()

        mark_logs("Node 0 generates 220 block",self.nodes,DEBUG_MODE)
        self.nodes[0].generate(220)
        self.sync_all()

        #generate wCertVk and constant
        mcTest = MCTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params('sc1')
        constant1 = generate_random_field_element_hex()

        amount = 1.0
        fee = 0.000025

        #-------------------------------------------------------
        bad_obj = {"a":1, "b":2}
        cmdInput = {
            'withdrawalEpochLength': EPOCH_LENGTH, 'vFieldElementCertificateFieldConfig': bad_obj,
            'toaddress': "abcd", 'amount': amount, 'fee': fee, 'wCertVk': vk}

        mark_logs("\nNode 1 create SC with wrong vFieldElementCertificateFieldConfig obj in input (expecting failure...)", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].create_sidechain(cmdInput)
            assert_true(False);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("not an array" in errorString)

        #-------------------------------------------------------
        bad_array = ["hello", "world"]
        cmdInput = {'vBitVectorCertificateFieldConfig': bad_array, 'toaddress': "abcd", 'amount': amount, 'fee': fee, 'wCertVk': vk}

        mark_logs("\nNode 1 create SC with wrong vBitVectorCertificateFieldConfig array in input (expecting failure...)", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].create_sidechain(cmdInput)
            assert_true(False);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("Invalid vBitVectorCertificateFieldConfig" in errorString)

        #-------------------------------------------------------
        too_large_array_values = [[1000761, 31]] # [1000192, 1000192/8] at most
        cmdInput = {'vBitVectorCertificateFieldConfig': too_large_array_values, 'toaddress': "abcd", 'amount': amount, 'fee': fee, 'wCertVk': vk}

        mark_logs("\nNode 1 create SC with a vBitVectorCertificateFieldConfig array with too large integers (expecting failure...)", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].create_sidechain(cmdInput)
            assert_true(False);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("invalid-custom-config" in errorString)

        #-------------------------------------------------------
        zero_values_array = [0, 0] 
        cmdInput = {'vFieldElementCertificateFieldConfig': zero_values_array, 'toaddress': "abcd", 'amount': amount, 'fee': fee, 'wCertVk': vk}

        mark_logs("\nNode 1 create SC with a vFieldElementCertificateFieldConfig array with zeroes (expecting failure...)", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].create_sidechain(cmdInput)
            assert_true(False);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("invalid-custom-config" in errorString)

        #-------------------------------------------------------
        fee = 0.000025
        feCfg = []
        cmtCfg = []

        # all certs must have custom FieldElements with exactly those values as size in bits 
        feCfg.append([31, 48, 16])

        cmtCfg.append([[254*8, 4], [254*8*2, 8]])

        cmdInput = {
            'withdrawalEpochLength': EPOCH_LENGTH, 'amount': amount, 'fee': fee,
            'constant':constant1 , 'wCertVk': vk, 'toaddress':"cdcd",
            'vFieldElementCertificateFieldConfig':feCfg[0], 'vBitVectorCertificateFieldConfig':cmtCfg[0] }

        mark_logs("\nNode 1 create SC1 with valid vFieldElementCertificateFieldConfig / vBitVectorCertificateFieldConfig pair", self.nodes,DEBUG_MODE)
        try:
            res = self.nodes[1].create_sidechain(cmdInput)
            tx =   res['txid']
            scid1 = res['scid']
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False);

        self.sync_all()

        mark_logs("Verify vFieldElementCertificateFieldConfig / vBitVectorCertificateFieldConfig are correctly set in creation tx", self.nodes,DEBUG_MODE)
        decoded_tx = self.nodes[1].getrawtransaction(tx, 1)
        dec_sc_id = decoded_tx['vsc_ccout'][0]['scid']
        assert_equal(scid1, dec_sc_id)

        feCfgStr  = decoded_tx['vsc_ccout'][0]['vFieldElementCertificateFieldConfig']
        cmtCfgStr = decoded_tx['vsc_ccout'][0]['vBitVectorCertificateFieldConfig']
        assert_equal(feCfgStr, feCfg[0])
        assert_equal(cmtCfgStr, cmtCfg[0])

        # test two more SC creations with different versions of the cmd
        #-------------------------------------------------------
        vk = mcTest.generate_params("sc2")
        constant2 = generate_random_field_element_hex()
        customData = "c0ffee"
        mbtrVk = ""     
        cswVk  = ""
        feCfg.append([16])
        cmtCfg.append([])

        mark_logs("\nNode 1 create SC2 with valid vFieldElementCertificateFieldConfig / vBitVectorCertificateFieldConfig pair", self.nodes,DEBUG_MODE)
        try:
            ret = self.nodes[1].sc_create(EPOCH_LENGTH, "dada", amount, vk, customData, constant2, mbtrVk, cswVk, feCfg[1], cmtCfg[1])
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False);
        self.sync_all()
 
        mark_logs("Verify vFieldElementCertificateFieldConfig / vBitVectorCertificateFieldConfig are correctly set in creation tx", self.nodes,DEBUG_MODE)
        creating_tx = ret['txid']
        scid2 = ret['scid']

        decoded_tx = self.nodes[1].getrawtransaction(creating_tx, 1)
        dec_sc_id = decoded_tx['vsc_ccout'][0]['scid']
        assert_equal(scid2, dec_sc_id)

        feCfgStr  = decoded_tx['vsc_ccout'][0]['vFieldElementCertificateFieldConfig']
        cmtCfgStr = decoded_tx['vsc_ccout'][0]['vBitVectorCertificateFieldConfig']
        assert_equal(feCfgStr, feCfg[1])
        assert_equal(cmtCfgStr, cmtCfg[1])

        #-------------------------------------------------------
        vk = mcTest.generate_params("sc3")
        constant3 = generate_random_field_element_hex()
        customData = "c0ffee"
        feCfg.append([])
        cmtCfg.append([[254*8*3, 1967]])

        sc_cr = [{
            "epoch_length": EPOCH_LENGTH, "amount":amount, "address":"ddaa", "wCertVk": vk, "constant": constant3,
            "vFieldElementCertificateFieldConfig":feCfg[2], "vBitVectorCertificateFieldConfig":cmtCfg[2] }]

        mark_logs("\nNode 0 create SC3 with valid vFieldElementCertificateFieldConfig / vBitVectorCertificateFieldConfig pair", self.nodes,DEBUG_MODE)
        try:
            rawtx=self.nodes[0].createrawtransaction([],{},[],sc_cr)
            funded_tx = self.nodes[0].fundrawtransaction(rawtx)
            sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
            creating_tx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False);
        self.sync_all()
 
        mark_logs("Verify vFieldElementCertificateFieldConfig / vBitVectorCertificateFieldConfig are correctly set in creation tx", self.nodes,DEBUG_MODE)
        decoded_tx = self.nodes[0].getrawtransaction(creating_tx, 1)
        scid3 = decoded_tx['vsc_ccout'][0]['scid']

        feCfgStr  = decoded_tx['vsc_ccout'][0]['vFieldElementCertificateFieldConfig']
        cmtCfgStr = decoded_tx['vsc_ccout'][0]['vBitVectorCertificateFieldConfig']
        assert_equal(feCfgStr, feCfg[2])
        assert_equal(cmtCfgStr, cmtCfg[2])

        #-------------------------------------------------------
        mark_logs("\nNode 0 generates 1 block confirming SC creations", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        sc_creating_height = self.nodes[0].getblockcount()
        self.sync_all()

        scids = [scid1, scid2, scid3]
        mark_logs("Verify vFieldElementCertificateFieldConfig / vBitVectorCertificateFieldConfig are correctly set in scinfo for all SCs", self.nodes,DEBUG_MODE)
        for i, val in enumerate(scids):
            feCfgStr  = self.nodes[0].getscinfo(val)['items'][0]['vFieldElementCertificateFieldConfig']
            cmtCfgStr = self.nodes[0].getscinfo(val)['items'][0]['vBitVectorCertificateFieldConfig']
            assert_equal(feCfgStr, feCfg[i])
            assert_equal(cmtCfgStr, cmtCfg[i])

        #-------------------------------------------------------
        # advance epoch
        certs = []
        mark_logs("\nNode 0 generates {} block".format(EPOCH_LENGTH-1), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(EPOCH_LENGTH - 1)
        self.sync_all()

        epoch_block_hash_1, epoch_number_1, epoch_cum_tree_hash_1 = get_epoch_data(scid1, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_block_hash = {}".format(epoch_number_1, epoch_block_hash_1), self.nodes, DEBUG_MODE)

        prev_epoch_block_hash = self.nodes[0].getblockhash(sc_creating_height - 1 + ((epoch_number_1) * EPOCH_LENGTH))

        #-------------------------------------------------------
        # do some negative test for having a raw cert rejected by mempool
        pkh_node1 = self.nodes[1].getnewaddress("", True)
        bwt_amount = Decimal("0.1")

        scProof2 = mcTest.create_test_proof(
            'sc2', epoch_number_1, epoch_block_hash_1, prev_epoch_block_hash,
            10, constant2, [pkh_node1], [bwt_amount])

        # get a UTXO
        utx, change = get_spendable(self.nodes[0], CERT_FEE)

        inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        outputs = { self.nodes[0].getnewaddress() : change }
        bwt_outs = {pkh_node1: bwt_amount}

        # cfgs for SC2: [16], []
        mark_logs("\nCreate raw cert with wrong field element for the referred SC2 (expecting failure)...", self.nodes, DEBUG_MODE)
        vCfe = ["abcd1234", "ccccddddeeee", "aaee"]
        vCmt = ["1111", "0660101a"]
        params = {
            'scid': scid2,
            'quality': 10,
            'endEpochBlockHash': epoch_block_hash_1,
            'scProof': scProof2,
            'withdrawalEpochNumber': epoch_number_1, 'vFieldElementCertificateField': vCfe, 'vBitVectorCertificateField':vCmt}
        try:
            rawcert    = self.nodes[0].createrawcertificate(inputs, outputs, bwt_outs, params)
            signed_cert = self.nodes[0].signrawcertificate(rawcert)
            self.nodes[0].sendrawcertificate(signed_cert['hex'])
            assert (False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        #-------------------------------------------------------
        mark_logs("\nCreate raw cert with good custom field elements for SC2...", self.nodes, DEBUG_MODE)
        # cfgs for SC2: [16], []
        # field element will be filled using trailing zeroes, therefore we must be careful with bits. "0x0100" = 0000 0001 0000 0000 is OK
        # because last 15 bits are set to 0
        vCfe = ["0100"]
        vCmt = []
        params = {
            'scid': scid2,
            'quality': 10,
            'endEpochBlockHash': epoch_block_hash_1,
            'endEpochCumScTxCommTreeRoot': epoch_cum_tree_hash_1,
            'scProof': scProof2,
            'withdrawalEpochNumber': epoch_number_1,
            'vFieldElementCertificateField': vCfe,
            'vBitVectorCertificateField':vCmt
        }

        try:
            rawcert = self.nodes[0].createrawcertificate(inputs, outputs, bwt_outs, params)
            signed_cert = self.nodes[0].signrawcertificate(rawcert)
            cert = self.nodes[0].sendrawcertificate(signed_cert['hex'])
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)
        self.sync_all()
        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert in self.nodes[0].getrawmempool())
        certs.append(cert);

        #-------------------------------------------------------
        scProof1 = mcTest.create_test_proof(
            'sc1', epoch_number_1, epoch_block_hash_1, prev_epoch_block_hash,
            10, constant1, [pkh_node1], [bwt_amount])

        # get another UTXO
        utx, change = get_spendable(self.nodes[0], CERT_FEE)
        inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        outputs = { self.nodes[0].getnewaddress() : change }

        # cfgs for SC1: [31, 48, 16], [[8, 4], [16, 8]]
        mark_logs("Create raw cert with bad custom field elements for SC1...", self.nodes, DEBUG_MODE)

        # In 0xcd there are no trailing null bits, therefore it is not OK
        vCfe = ["abde12cd", "ccccbbbbeeee", "cbbd"]
        vCmt = ["1111", "0660101a"]
        params = {
            'scid': scid1,
            'quality': 10,
            'endEpochBlockHash': epoch_block_hash_1,
            'endEpochCumScTxCommTreeRoot': epoch_cum_tree_hash_1,
            'scProof': scProof1,
            'withdrawalEpochNumber': epoch_number_1,
            'vFieldElementCertificateField': vCfe,
            'vBitVectorCertificateField':vCmt
        }

        try:
            rawcert    = self.nodes[0].createrawcertificate(inputs, outputs, bwt_outs, params)
            signed_cert = self.nodes[0].signrawcertificate(rawcert)
            cert = self.nodes[0].sendrawcertificate(signed_cert['hex'])
            assert (False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        self.sync_all()
        mark_logs("\nCreate raw cert with good custom field elements for SC1...", self.nodes, DEBUG_MODE)

        # Any number ending with 0x00 0x01 0x00 is not over module for being a valid field element, therefore it is OK
        vCfe = ["ab000100", "ccccdddd0000", "0100"]
        vCmt = ["1111", "0660101a"]
        params = {
            'scid': scid1,
            'quality': 10,
            'endEpochBlockHash': epoch_block_hash_1,
            'endEpochCumScTxCommTreeRoot': epoch_cum_tree_hash_1,
            'scProof': scProof1,
            'withdrawalEpochNumber': epoch_number_1,
            'vFieldElementCertificateField': vCfe,
            'vBitVectorCertificateField':vCmt
        }

        try:
            rawcert    = self.nodes[0].createrawcertificate(inputs, outputs, bwt_outs, params)
            signed_cert = self.nodes[0].signrawcertificate(rawcert)
            cert = self.nodes[0].sendrawcertificate(signed_cert['hex'])
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)
        self.sync_all()
        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert in self.nodes[0].getrawmempool())
        certs.append(cert);

        #-------------------------------------------------------
        mark_logs("\nCreate Cert for SC3 with good custom field elements", self.nodes, DEBUG_MODE)
        scProof3 = mcTest.create_test_proof(
            'sc3', epoch_number_1, epoch_block_hash_1, prev_epoch_block_hash,
            5, constant3, [], [])

        vCfe = []
        vCmt = ["1122334455667788"]
        try:
            cert = self.nodes[0].send_certificate(scid3, epoch_number_1, 5, epoch_block_hash_1,
            epoch_cum_tree_hash_1, scProof3, [], CERT_FEE, vCfe, vCmt)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)
        self.sync_all()
        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert in self.nodes[0].getrawmempool())
        certs.append(cert);

        # advance epoch
        #-------------------------------------------------------
        mark_logs("\nNode 0 generates 1 block confirming certs", self.nodes, DEBUG_MODE)
        bestHash = self.nodes[0].generate(1)[-1]
        self.sync_all()

        # check all certs for this epoch are correctly mined
        blist = self.nodes[0].getblock(bestHash, True)['cert']
        for x in certs:
            assert_true(x in blist)
        
        mark_logs("\nNode 0 generates 4 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(4)
        self.sync_all()

        epoch_block_hash_2, epoch_number_2, epoch_cum_tree_hash_2 = get_epoch_data(scid1, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_block_hash = {}".format(epoch_number_2, epoch_block_hash_2), self.nodes, DEBUG_MODE)

        prev_epoch_block_hash = epoch_block_hash_1

        #-------------------------------------------------------
        # cfgs for SC1: [31, 48, 16], [[8, 4], [16, 8]]
        scProof1 = mcTest.create_test_proof(
            'sc1', epoch_number_2, epoch_block_hash_2, prev_epoch_block_hash,
            5, constant1, [], [])

        mark_logs("\nCreate Cert without custom field elements (should fail)", self.nodes, DEBUG_MODE)
        try:
            self.nodes[0].send_certificate(scid1, epoch_number_2, 5, epoch_block_hash_2,
            epoch_cum_tree_hash_2, scProof1, [], CERT_FEE)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        #-------------------------------------------------------
        mark_logs("\nCreate Cert with invalid custom field elements (should fail)", self.nodes, DEBUG_MODE)
        # vCfe[0]---> 256 bits (!= 31)
        vCfe = ["06601c01528416d44682d41d979ded016d950924418ec354663f0bd761188da3", "0912f922dd37b01258eaf5311d68e723f8a8ced4a3c64471511b0020bf3fdcc9"]
        vCmt = ["6d950924418ec337b01258eaf5311d68e723f8a8ced4", "233311860324"]
        try:
            self.nodes[0].send_certificate(scid1, epoch_number_2, 5, epoch_block_hash_2,
            epoch_cum_tree_hash_2, scProof1, [], CERT_FEE, vCfe, vCmt)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        #-------------------------------------------------------
        mark_logs("\nCreate Cert with invalid custom field elements (should fail)", self.nodes, DEBUG_MODE)
        vCfe = ["18ec3546", "12f922dd37b0", "dddd"]
        vCmt = ["6d950924418ec337b01258eaf5311d68e723f8a8ced4", "23331186032400aaff"]
        # vCmt[1]---> 9 bytes (!= 2^3 * 8 = 64 bits)
        try:
            self.nodes[0].send_certificate(scid1, epoch_number_2, 5, epoch_block_hash_2,
            epoch_cum_tree_hash_2, scProof1, [], CERT_FEE, vCfe, vCmt)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        return

        #-------------------------------------------------------
        mark_logs("\nCreate Cert for SC1 with good custom field elements", self.nodes, DEBUG_MODE)
        vCfe = ["18ec3546", "12f922dd37b0", "abcd"]
        vCmt = ["6d950924418ec337b01258eaf5311d68e723f8a8ced4", "23331186032400ff"]
        try:
            cert = self.nodes[0].send_certificate(scid1, epoch_number_2, 5, epoch_block_hash_2,
            epoch_cum_tree_hash_2, scProof1, [], CERT_FEE, vCfe, vCmt)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)
        self.sync_all()
        mark_logs("\nCheck cert is in mempool", self.nodes, DEBUG_MODE)
        assert_equal(True, cert in self.nodes[0].getrawmempool())

        #-------------------------------------------------------
        # parse a good cert and check custom fields
        mark_logs("\nVerify vFieldElementCertificateField/ vBitVectorCertificateFieldare correctly set in cert", self.nodes,DEBUG_MODE)
        decoded = self.nodes[1].getrawcertificate(cert, 1)

        vCfeCert = decoded['cert']['vFieldElementCertificateField']
        assert_equal(vCfeCert, vCfe)
        for i, val in enumerate(vCfe):
            assert_equal(val, vCfeCert[i]) 

        vCmtCert = decoded['cert']['vBitVectorCertificateField']
        assert_equal(vCmtCert, vCmt)
        for i, val in enumerate(vCmt):
            assert_equal(val, vCmtCert[i]) 

        bestHash = self.nodes[0].generate(1)[-1]
        self.sync_all()

        # check that the cert for this epoch is correctly mined
        assert_true(cert in self.nodes[0].getblock(bestHash, True)['cert'])
        
        mark_logs("...stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        # check it again
        assert_true(cert in self.nodes[1].getblock(bestHash, True)['cert'])
        


if __name__ == '__main__':
    sc_cert_customfields().main()
