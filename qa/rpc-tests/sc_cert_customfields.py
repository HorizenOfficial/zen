#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
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
        constant = generate_random_field_element_hex()

        amount = 12.0
        fee = 0.000025

        # Negative tests for sc creation
        #-------------------------------------------------------
        bad_obj = {"a":1, "b":2}

        cmdInput = {
            'withdrawalEpochLength': EPOCH_LENGTH, 'vFieldElementConfig': bad_obj,
            'toaddress': "abcd", 'amount': amount, 'fee': fee, 'wCertVk': vk}

        mark_logs("\nNode 1 create SC with wrong vFieldElementConfig obj in input", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].create_sidechain(cmdInput)
            assert_true(False);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("not an array" in errorString)

        #-------------------------------------------------------
        bad_array = ["hello", "world"]

        cmdInput = {'vCompressedMerkleTreeConfig': bad_array, 'toaddress': "abcd", 'amount': amount, 'fee': fee, 'wCertVk': vk}

        mark_logs("\nNode 1 create SC with wrong vCompressedMerkleTreeConfig array in input", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].create_sidechain(cmdInput)
            assert_true(False);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("expected int" in errorString)

        #-------------------------------------------------------
        too_large_array_values = [30, 31]

        cmdInput = {'vCompressedMerkleTreeConfig': too_large_array_values, 'toaddress': "abcd", 'amount': amount, 'fee': fee, 'wCertVk': vk}

        mark_logs("\nNode 1 create SC with wrong vCompressedMerkleTreeConfig array (too large integers)", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].create_sidechain(cmdInput)
            assert_true(False);
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true("too large" in errorString)

        # Node 1 create the SC using a valid vFieldElementConfig / vCompressedMerkleTreeConfig pair
        #------------------------------------------------------------------------------------------
        mark_logs("\nNode 1 create SC with valid vFieldElementConfig / vCompressedMerkleTreeConfig pair", self.nodes,DEBUG_MODE)

        amount = 20.0
        fee = 0.000025

        # all certs must have custom FieldElements with exactly those values as size in byte 
        feCfg = [4, 6]

        # all certs must have custom CompressedMerkleTrees with size = 2^those value at most in byte (i.e 2^8 = 256, 2^3 = 2048)
        cmtCfg = [8, 3]

        cmdInput = {
            'withdrawalEpochLength': EPOCH_LENGTH, 'amount': amount, 'fee': fee,
            'constant':constant , 'wCertVk': vk, 'toaddress':"cdcd",
            'vFieldElementConfig':feCfg, 'vCompressedMerkleTreeConfig':cmtCfg
        }

        try:
            res = self.nodes[1].create_sidechain(cmdInput)
            tx =   res['txid']
            scid = res['scid']
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False);

        self.sync_all()

        mark_logs("\nVerify vFieldElementConfig / vCompressedMerkleTreeConfig are correctly set in tx", self.nodes,DEBUG_MODE)
        decoded_tx = self.nodes[1].getrawtransaction(tx, 1)
        sc_id = decoded_tx['vsc_ccout'][0]['scid']
        assert_equal(scid, sc_id)

        feCfgStr  = decoded_tx['vsc_ccout'][0]['vFieldElementConfig']
        cmtCfgStr = decoded_tx['vsc_ccout'][0]['vCompressedMerkleTreeConfig']
        assert_equal([int(x) for x in feCfgStr.split()], feCfg)
        assert_equal([int(x) for x in cmtCfgStr.split()], cmtCfg)

        mark_logs("\nNode 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        sc_creating_height = self.nodes[0].getblockcount()
        self.sync_all()

        mark_logs("\nVerify vFieldElementConfig / vCompressedMerkleTreeConfig are correctly set in scinfo", self.nodes,DEBUG_MODE)
        feCfgStr  = self.nodes[0].getscinfo(sc_id)['items'][0]['vFieldElementConfig']
        cmtCfgStr = self.nodes[0].getscinfo(sc_id)['items'][0]['vCompressedMerkleTreeConfig']
        assert_equal([int(x) for x in feCfgStr.split()], feCfg)
        assert_equal([int(x) for x in cmtCfgStr.split()], cmtCfg)

        # advance epoch
        mark_logs("\nNode 0 generates 4 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(4)
        self.sync_all()

        epoch_block_hash_1, epoch_number_1 = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_block_hash = {}".format(epoch_number_1, epoch_block_hash_1), self.nodes, DEBUG_MODE)

        prev_epoch_block_hash = self.nodes[0].getblockhash(sc_creating_height - 1 + ((epoch_number_1) * EPOCH_LENGTH))

        # TODO do some negative test for having a raw cert rejected by mempool

        mark_logs("Create Cert with custom field elements", self.nodes, DEBUG_MODE)
        pkh_node1 = self.nodes[1].getnewaddress("", True)
        bwt_amount = Decimal("0.1")

        scProof = mcTest.create_test_proof(
            'sc1', epoch_number_1, epoch_block_hash_1, prev_epoch_block_hash,
            10, constant, [pkh_node1], [bwt_amount])

        # get a UTXO
        utx, change = get_spendable(self.nodes[0], CERT_FEE)

        vFe = ["abcd1234", "ccccddddeeee"]
        vCmt = ["1111", ""]

        inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        outputs = { self.nodes[0].getnewaddress() : change }
        bwt_outs = {pkh_node1: bwt_amount}
        params = {'scid': scid, 'quality': 10, 'endEpochBlockHash': epoch_block_hash_1, 'scProof': scProof,
                  'withdrawalEpochNumber': epoch_number_1, 'vFieldElement': vFe, 'vCompressedMerkleTree':vCmt}

        try:
            rawcert    = self.nodes[0].createrawcertificate(inputs, outputs, bwt_outs, params)
            signed_cert = self.nodes[0].signrawcertificate(rawcert)
            cert = self.nodes[0].sendrawcertificate(signed_cert['hex'])
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert in self.nodes[0].getrawmempool())

        # advance epoch
        mark_logs("\nNode 0 generates 5 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()

        epoch_block_hash_2, epoch_number_2 = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_block_hash = {}".format(epoch_number_2, epoch_block_hash_2), self.nodes, DEBUG_MODE)

        prev_epoch_block_hash = epoch_block_hash_1

        scProof = mcTest.create_test_proof(
            'sc1', epoch_number_2, epoch_block_hash_2, prev_epoch_block_hash,
            5, constant, [], [])

        mark_logs("Create Cert without custom field elements (should fail)", self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[0].send_certificate(scid, epoch_number_2, 5, epoch_block_hash_2, scProof, [], CERT_FEE)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        mark_logs("Create Cert with invalid custom field elements (should fail)", self.nodes, DEBUG_MODE)

        vFe = ["06601c01528416d44682d41d979ded016d950924418ec354663f0bd761188da3", "0912f922dd37b01258eaf5311d68e723f8a8ced4a3c64471511b0020bf3fdcc9"]
        vCmt = ["6d950924418ec337b01258eaf5311d68e723f8a8ced4", "233311860324"]

        try:
            cert = self.nodes[0].send_certificate(scid, epoch_number_2, 5, epoch_block_hash_2, scProof, [], CERT_FEE, vFe, vCmt)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        mark_logs("Create Cert with invalid custom field elements (should fail)", self.nodes, DEBUG_MODE)

        vFe = ["18ec3546", "12f922dd37b0"]
        vCmt = ["6d950924418ec337b01258eaf5311d68e723f8a8ced4", "23331186032400aaff"]

        try:
            cert = self.nodes[0].send_certificate(scid, epoch_number_2, 5, epoch_block_hash_2, scProof, [], CERT_FEE, vFe, vCmt)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        self.sync_all()

        # TODO parse a good cert for getting wanted custom fields
        # TODO mine a cert
        # TODO restart nodes

        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert in self.nodes[0].getrawmempool())


if __name__ == '__main__':
    sc_cert_customfields().main()
