#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework, ForkHeights
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_node, connect_nodes_bi, assert_true, assert_false, get_epoch_data, \
    swap_bytes, mark_logs
from test_framework.mc_test.mc_test import CertTestUtils, generate_random_field_element_hex
from test_framework.authproxy import JSONRPCException

from decimal import Decimal

DEBUG_MODE = 1
EPOCH_LENGTH = 100
CERT_FEE = Decimal("0.000123")
SC_FEE = Decimal("0.000345")
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')

class getblockexpanded(BitcoinTestFramework):
    FEE = 0.0001
    FT_SC_FEE = Decimal('0')
    MBTR_SC_FEE = Decimal('0')
    CERT_FEE = Decimal('0.00015')

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 2)
        self.first_round = True

    def setup_network(self, split=False):
        self.nodes = []
        self.nodes += [start_node(0, self.options.tmpdir,extra_args=['-txindex=1','-maturityheightindex=1'])]
        self.nodes += [start_node(1, self.options.tmpdir)]

        connect_nodes_bi(self.nodes,0,1)

        self.is_network_split=False
        self.sync_all()

    def check_equal(self, a, b):
        if len(a) != len(b):
            return False
        for x in a:
            if x not in b:
                return False
        return True

    def run_test_ceasable(self, scversion):

        # sc name
        sc_name = "sc" + str(scversion)

        #amounts
        creation_amount = Decimal("50")
        bwt_amount = Decimal("5")
        tAddr1 = self.nodes[1].getnewaddress()
        node1Addr = self.nodes[1].validateaddress(tAddr1)['address']
        if self.first_round:
            self.nodes[0].generate(ForkHeights['NON_CEASING_SC'])
            self.sync_all()
            self.first_round = False
            self.cert_dict = dict()
            self.cert_dict_non_ceasing = dict()
            self.round_number = 0
            self.blck_h = []
            self.mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)

        ########### Create the sidechain ##################
        mark_logs("########### Create the sidechain ##################", self.nodes, DEBUG_MODE)

        vk = self.mcTest.generate_params(sc_name) if scversion < 2 else self.mcTest.generate_params(sc_name, keyrot = True)
        self.constant = generate_random_field_element_hex()

        cmdInput = {
            "version": scversion,
            'withdrawalEpochLength': EPOCH_LENGTH,
            'toaddress': "dada",
            'amount': creation_amount,
            'wCertVk': vk,
            'constant': self.constant
        }

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        self.sync_all()

        decoded_tx = self.nodes[0].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])

        sc_creation_block_hash = self.nodes[0].generate(1)[0]
        sc_creation_block = self.nodes[0].getblock(sc_creation_block_hash)
        self.sync_all()

        #Advance for 1 Epoch
        self.nodes[0].generate(EPOCH_LENGTH)
        self.sync_all()

        ########### Mine Certificate 1 with quality = 5 ##################
        mark_logs("########### Mine Certificate 1 with quality = 5 ##################", self.nodes, DEBUG_MODE)
        epoch_number, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        quality = 5
        proof = self.mcTest.create_test_proof(sc_name,
                                              scid_swapped,
                                              epoch_number,
                                              quality,
                                              MBTR_SC_FEE,
                                              FT_SC_FEE,
                                              epoch_cum_tree_hash, 
                                              prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                              constant       = self.constant,
                                              pks            = [node1Addr],
                                              amounts        = [bwt_amount])

        amount_cert_1 = [{"address": node1Addr, "amount": bwt_amount}]

        self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.sync_all()
        cert3_maturityHeight = sc_creation_block["height"]+(EPOCH_LENGTH*2)+EPOCH_LENGTH*0.2 - 1

        #Add to mempool Certificate 2 with quality = 7
        mark_logs("########### Add to mempool Certificate 2 with quality = 7 ##################", self.nodes, DEBUG_MODE)
        quality = 7  
        bwt_amount2 = Decimal("7")
        proof = self.mcTest.create_test_proof(sc_name,
                                              scid_swapped,
                                              epoch_number,
                                              quality,
                                              MBTR_SC_FEE,
                                              FT_SC_FEE,
                                              epoch_cum_tree_hash, 
                                              prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                              constant       = self.constant,
                                              pks            = [node1Addr],
                                              amounts        = [bwt_amount2])

        amount_cert_2 = [{"address": node1Addr, "amount": bwt_amount2}]

        self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_2, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.sync_all()

        #Mine a block
        cert2_block = self.nodes[0].generate(1)
        self.sync_all()

        #Mine a block with a new Certificate 3 with quality = 8
        mark_logs("########### Mine a block with a new Certificate 3 with quality = 8 ##################", self.nodes, DEBUG_MODE)
        quality = 8
        bwt_amount3 = Decimal("7")
        proof = self.mcTest.create_test_proof(sc_name,
                                              scid_swapped,
                                              epoch_number,
                                              quality,
                                              MBTR_SC_FEE,
                                              FT_SC_FEE,
                                              epoch_cum_tree_hash, 
                                              prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                              constant       = self.constant,
                                              pks            = [node1Addr],
                                              amounts        = [bwt_amount3])

        amount_cert_3 = [{"address": node1Addr, "amount": bwt_amount3}]

        cert3 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_3, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.sync_all()
        cert3_block = self.nodes[0].generate(1)[0]
        self.sync_all()

        #Advance of 1 epoch
        mark_logs("########### Advance of 1 epoch ##################", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(116)
        self.sync_all()

        mark_logs("########### Add to mempool Certificate 4 with quality = 9 ##################", self.nodes, DEBUG_MODE)
        epoch_number, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        if scversion < 2:
            prev_cert_hash = None

        quality = 9
        bwt_amount4 = Decimal("9")
        proof = self.mcTest.create_test_proof(sc_name,
                                              scid_swapped,
                                              epoch_number,
                                              quality,
                                              MBTR_SC_FEE,
                                              FT_SC_FEE,
                                              epoch_cum_tree_hash, 
                                              prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                              constant       = self.constant,
                                              pks            = [node1Addr],
                                              amounts        = [bwt_amount4])

        amount_cert_4 = [{"address": node1Addr, "amount": bwt_amount4}]

        self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_4, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)

        cert4_block = self.nodes[0].generate(1)[0]
        self.sync_all()

        #Enter in the next epoch
        new_epoch_block = self.nodes[0].generate(1)[0]
        self.sync_all()

        rpcCertBlock = self.nodes[0].getblock(cert3_block, 2)
        cert_3_json = {}
        for cert in rpcCertBlock['cert']:
            if (cert['txid'] == cert3):
                cert_3_json = cert
        assert_true(cert_3_json != {})
        tipHeight = self.nodes[0].getblockcount()

        # Add infos about cert3 of this round
        self.cert_dict[int(cert3_maturityHeight)] = {"cert": [cert3], "cert_json": [cert_3_json], "number": 1}
        self.blck_h.append(int(cert3_maturityHeight) - 1)

        #Test that we require -maturityheightindex=1 to run the getblockexpanded
        try:
            self.nodes[1].getblockexpanded("700")
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert("maturityHeightIndex option not set: can not retrieve info" in errorString)

        #Test that we see the certificate 3 but non the certificate 2 and 1
        for i in range (1,tipHeight+1):
            rpcDataByHeight = self.nodes[0].getblockexpanded(str(i))
            rpcDataByHeightVerbosity = self.nodes[0].getblockexpanded(str(i), 2)
            rpcDataByHash = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'])
            rpcDataByHashVerbosity = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'],2)

            if (rpcDataByHash['height'] >= ForkHeights['MINIMAL_SC']):
                assert_true('matureCertificate' in rpcDataByHash)
                assert_true('matureCertificate' in rpcDataByHeight)
                assert_true('matureCertificate' in rpcDataByHashVerbosity)
                assert_true('matureCertificate' in rpcDataByHeightVerbosity)
                if (rpcDataByHash['height'] in self.cert_dict):
                    assert_equal(len(rpcDataByHash['matureCertificate']),                       self.cert_dict[rpcDataByHash['height']]["number"])
                    assert_equal(len(rpcDataByHeight['matureCertificate']),                     self.cert_dict[rpcDataByHash['height']]["number"])
                    assert_true(self.check_equal(rpcDataByHash['matureCertificate'],            self.cert_dict[rpcDataByHash['height']]["cert"]))
                    assert_true(self.check_equal(rpcDataByHeight['matureCertificate'],          self.cert_dict[rpcDataByHash['height']]["cert"]))
                    assert_equal(len(rpcDataByHashVerbosity['matureCertificate']),              self.cert_dict[rpcDataByHash['height']]["number"])
                    assert_equal(len(rpcDataByHeightVerbosity['matureCertificate']),            self.cert_dict[rpcDataByHash['height']]["number"])
                    assert_true(self.check_equal(rpcDataByHashVerbosity['matureCertificate'],   self.cert_dict[rpcDataByHash['height']]["cert_json"]))
                    assert_true(self.check_equal(rpcDataByHeightVerbosity['matureCertificate'], self.cert_dict[rpcDataByHash['height']]["cert_json"]))
                else:
                    assert_equal(len(rpcDataByHash['matureCertificate']), 0)
                    assert_equal(len(rpcDataByHeight['matureCertificate']), 0)
                    assert_equal(len(rpcDataByHashVerbosity['matureCertificate']), 0)
                    assert_equal(len(rpcDataByHeightVerbosity['matureCertificate']), 0)
            else:
                assert_false('matureCertificate' in rpcDataByHash)
                assert_false('matureCertificate' in rpcDataByHeight)
                assert_false('matureCertificate' in rpcDataByHashVerbosity)
                assert_false('matureCertificate' in rpcDataByHeightVerbosity)

        self.nodes[0].invalidateblock(new_epoch_block)
        self.nodes[0].invalidateblock(cert4_block)
        self.nodes[1].invalidateblock(new_epoch_block)
        self.nodes[1].invalidateblock(cert4_block)
        assert_equal(self.nodes[0].getblockcount(), self.blck_h[self.round_number])
        assert_equal(self.nodes[1].getblockcount(), self.blck_h[self.round_number])

        self.nodes[0].clearmempool()
        self.nodes[1].clearmempool()
        assert_equal(len(self.nodes[0].getrawmempool()),0)
        assert_equal(len(self.nodes[1].getrawmempool()),0)
        self.sync_all()

        fake_block = self.nodes[0].generate(1)[0]
        self.sync_all()

        blcknum = str(int(self.blck_h[self.round_number]) + 1) # "700", "1051"

        rpcDataByHeight = self.nodes[0].getblockexpanded(blcknum)
        rpcDataByHeightVerbosity = self.nodes[0].getblockexpanded(blcknum, 2)
        rpcDataByHash = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'])
        rpcDataByHashVerbosity = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'],2)
        assert_equal(len(rpcDataByHash['matureCertificate']), 0)
        assert_equal(len(rpcDataByHeight['matureCertificate']), 0)
        assert_equal(len(rpcDataByHashVerbosity['matureCertificate']), 0)
        assert_equal(len(rpcDataByHeightVerbosity['matureCertificate']), 0)

        self.nodes[0].invalidateblock(fake_block)
        self.nodes[1].invalidateblock(fake_block)

        self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_4, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.nodes[0].generate(2)
        self.sync_all()

        rpcDataByHeight = self.nodes[0].getblockexpanded(blcknum)
        rpcDataByHeightVerbosity = self.nodes[0].getblockexpanded(blcknum, 2)
        rpcDataByHash = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'])
        rpcDataByHashVerbosity = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'],2)
        assert_equal(len(rpcDataByHash['matureCertificate']), 1)
        assert_equal(len(rpcDataByHeight['matureCertificate']), 1)
        assert_equal(len(rpcDataByHashVerbosity['matureCertificate']), 1)
        assert_equal(len(rpcDataByHeightVerbosity['matureCertificate']), 1)
        assert_equal(rpcDataByHashVerbosity['matureCertificate'][0], cert_3_json)
        assert_equal(rpcDataByHeightVerbosity['matureCertificate'][0], cert_3_json)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['state'], "ALIVE")

        #Let the sidechain cease
        self.nodes[0].generate(130)
        self.sync_all()

        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['state'], "CEASED")
        tipHeightCeased = self.nodes[0].getblockcount()

        for i in range (tipHeight, tipHeightCeased+1):
            rpcDataByHeight = self.nodes[0].getblockexpanded(str(i))
            rpcDataByHeightVerbosity = self.nodes[0].getblockexpanded(str(i), 2)
            rpcDataByHash = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'])
            rpcDataByHashVerbosity = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'],2)

            assert_equal(len(rpcDataByHash['matureCertificate']), 0)
            assert_equal(len(rpcDataByHeight['matureCertificate']), 0)
            assert_equal(len(rpcDataByHashVerbosity['matureCertificate']), 0)
            assert_equal(len(rpcDataByHeightVerbosity['matureCertificate']), 0)

        self.round_number = self.round_number + 1
        self.sync_all()

    def run_test_non_ceasable(self, scversion):

        # sc name
        sc_name = "sc" + str(scversion) + "nonceas"

        #amounts
        creation_amount = Decimal("50")
        bwt_amount = Decimal("5")
        tAddr1 = self.nodes[1].getnewaddress()
        node1Addr = self.nodes[1].validateaddress(tAddr1)['address']
        if self.first_round:
            self.nodes[0].generate(ForkHeights['NON_CEASING_SC'])
            self.sync_all()
            self.first_round = False
            self.cert_dict = dict()
            self.cert_dict_non_ceasing = dict()
            self.round_number = 0
            self.blck_h = []
            self.mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)

        ########### Create the sidechain ##################
        mark_logs("########### Create the sidechain ##################", self.nodes, DEBUG_MODE)

        vk = self.mcTest.generate_params(sc_name, keyrot = True)
        self.constant = generate_random_field_element_hex()

        cmdInput = {
            "version": scversion,
            'withdrawalEpochLength': 0,
            'toaddress': "dada",
            'amount': creation_amount,
            'wCertVk': vk,
            'constant': self.constant
        }

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        self.sync_all()

        decoded_tx = self.nodes[0].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])

        # Mine a block to validate sc creation
        sc_creation_block_hash = self.nodes[0].generate(1)[0]
        sc_creation_block = self.nodes[0].getblock(sc_creation_block_hash)
        sc_creation_height = self.nodes[0].getblockcount()
        self.sync_all()

        # Advance for 5 blocks
        self.nodes[0].generate(5)
        self.sync_all()

        # Add to mempool cert1 referring to (current block - 3)
        mark_logs("########### Add Certificate 1 to mempool ##################", self.nodes, DEBUG_MODE)
        curr_height = self.nodes[0].getblockcount()
        epoch_number, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH, is_non_ceasing = True, reference_height = curr_height - 3)
        quality = 1
        proof = self.mcTest.create_test_proof(sc_name,
                                              scid_swapped,
                                              epoch_number,
                                              quality,
                                              MBTR_SC_FEE,
                                              FT_SC_FEE,
                                              epoch_cum_tree_hash, 
                                              prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                              constant       = self.constant,
                                              pks            = [node1Addr],
                                              amounts        = [bwt_amount])


        amount_cert_1 = [{"address": node1Addr, "amount": bwt_amount}]

        cert1 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.sync_all()

        # Mine 3 blocks, the first one containing cert1
        mark_logs("########### Mine 3 blocks ##################", self.nodes, DEBUG_MODE)
        cert1_block = self.nodes[0].generate(1)[0]
        self.sync_all()
        cert1_maturityHeight = self.nodes[0].getblockcount()
        self.nodes[0].generate(2)
        self.sync_all()

        # Add to mempool cert2 referring to (current block - 1)
        mark_logs("########### Add Certificate 2 to mempool ##################", self.nodes, DEBUG_MODE)
        curr_height = self.nodes[0].getblockcount()
        epoch_number, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH, is_non_ceasing = True)
        bwt_amount2 = Decimal("8")
        proof = self.mcTest.create_test_proof(sc_name,
                                              scid_swapped,
                                              epoch_number,
                                              quality,
                                              MBTR_SC_FEE,
                                              FT_SC_FEE,
                                              epoch_cum_tree_hash, 
                                              prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                              constant       = self.constant,
                                              pks            = [node1Addr],
                                              amounts        = [bwt_amount2])

        amount_cert_2 = [{"address": node1Addr, "amount": bwt_amount2}]

        cert2 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_2, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.sync_all()

        # Mine 3 blocks, the first one containing cert2
        mark_logs("########### Mine 3 blocks ##################", self.nodes, DEBUG_MODE)
        cert2_block = self.nodes[0].generate(1)[0]
        self.sync_all()
        cert2_maturityHeight = self.nodes[0].getblockcount()
        self.nodes[0].generate(2)
        self.sync_all()

        # Add to mempool cert3 referring to (current block - 1)
        mark_logs("########### Add Certificate 3 to mempool ##################", self.nodes, DEBUG_MODE)
        curr_height = self.nodes[0].getblockcount()
        epoch_number, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH, is_non_ceasing = True, reference_height = curr_height - 1)
        bwt_amount3 = Decimal("9")
        proof = self.mcTest.create_test_proof(sc_name,
                                              scid_swapped,
                                              epoch_number,
                                              quality,
                                              MBTR_SC_FEE,
                                              FT_SC_FEE,
                                              epoch_cum_tree_hash, 
                                              prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                              constant       = self.constant,
                                              pks            = [node1Addr],
                                              amounts        = [bwt_amount3])

        amount_cert_3 = [{"address": node1Addr, "amount": bwt_amount3}]

        cert3 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_3, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.sync_all()

        # Mine 1 block containing cert3
        mark_logs("########### Mine 2 blocks ##################", self.nodes, DEBUG_MODE)
        cert3_block = self.nodes[0].generate(1)[0]
        self.sync_all()
        cert3_maturityHeight = self.nodes[0].getblockcount()
        new_epoch_block = self.nodes[0].generate(1)[0]
        self.sync_all()

        #### Start tests
        rpcCertBlock = self.nodes[0].getblock(cert2_block, 2)
        cert_2_json = {}
        for cert in rpcCertBlock['cert']:
            if (cert['txid'] == cert2):
                cert_2_json = cert
        assert_true(cert_2_json != {})

        rpcCertBlock = self.nodes[0].getblock(cert3_block, 2)
        cert_3_json = {}
        for cert in rpcCertBlock['cert']:
            if (cert['txid'] == cert3):
                cert_3_json = cert

        rpcCertBlock = self.nodes[0].getblock(cert1_block, 2)
        cert_1_json = {}
        for cert in rpcCertBlock['cert']:
            if (cert['txid'] == cert1):
                cert_1_json = cert

        tipHeight = self.nodes[0].getblockcount()

        # Add infos about certs to the dict
        self.cert_dict[int(cert1_maturityHeight)]               = {"cert": [cert1], "cert_json": [cert_1_json], "number": 1}
        self.cert_dict_non_ceasing[int(cert1_maturityHeight)]   = {"cert": [cert1], "cert_json": [cert_1_json], "number": 1}
        self.blck_h.append(int(cert1_maturityHeight) - 1)
        self.cert_dict[int(cert2_maturityHeight)]               = {"cert": [cert2], "cert_json": [cert_2_json], "number": 1}
        self.cert_dict_non_ceasing[int(cert2_maturityHeight)]   = {"cert": [cert2], "cert_json": [cert_2_json], "number": 1}
        self.blck_h.append(int(cert2_maturityHeight) - 1)
        self.cert_dict[int(cert3_maturityHeight)]               = {"cert": [cert3], "cert_json": [cert_3_json], "number": 1}
        self.cert_dict_non_ceasing[int(cert3_maturityHeight)]   = {"cert": [cert3], "cert_json": [cert_3_json], "number": 1}
        self.blck_h.append(int(cert3_maturityHeight) - 1)

        #Test that we require -maturityheightindex=1 to run the getblockexpanded
        try:
            self.nodes[1].getblockexpanded("700")
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert("maturityHeightIndex option not set: can not retrieve info" in errorString)

        #Test that we see the certificate all the certificates
        for i in range (1,tipHeight+1):
            rpcDataByHeight = self.nodes[0].getblockexpanded(str(i))
            rpcDataByHeightVerbosity = self.nodes[0].getblockexpanded(str(i), 2)
            rpcDataByHash = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'])
            rpcDataByHashVerbosity = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'],2)

            if (rpcDataByHash['height'] >= ForkHeights['MINIMAL_SC']):
                assert_true('matureCertificate' in rpcDataByHash)
                assert_true('matureCertificate' in rpcDataByHeight)
                assert_true('matureCertificate' in rpcDataByHashVerbosity)
                assert_true('matureCertificate' in rpcDataByHeightVerbosity)
                if (rpcDataByHash['height'] in self.cert_dict):
                    assert_equal(len(rpcDataByHash['matureCertificate']),                       self.cert_dict[rpcDataByHash['height']]["number"])
                    assert_equal(len(rpcDataByHeight['matureCertificate']),                     self.cert_dict[rpcDataByHash['height']]["number"])
                    assert_true(self.check_equal(rpcDataByHash['matureCertificate'],            self.cert_dict[rpcDataByHash['height']]["cert"]))
                    assert_true(self.check_equal(rpcDataByHeight['matureCertificate'],          self.cert_dict[rpcDataByHash['height']]["cert"]))
                    assert_equal(len(rpcDataByHashVerbosity['matureCertificate']),              self.cert_dict[rpcDataByHash['height']]["number"])
                    assert_equal(len(rpcDataByHeightVerbosity['matureCertificate']),            self.cert_dict[rpcDataByHash['height']]["number"])
                    assert_true(self.check_equal(rpcDataByHashVerbosity['matureCertificate'],   self.cert_dict[rpcDataByHash['height']]["cert_json"]))
                    assert_true(self.check_equal(rpcDataByHeightVerbosity['matureCertificate'], self.cert_dict[rpcDataByHash['height']]["cert_json"]))
                else:
                    assert_equal(len(rpcDataByHash['matureCertificate']), 0)
                    assert_equal(len(rpcDataByHeight['matureCertificate']), 0)
                    assert_equal(len(rpcDataByHashVerbosity['matureCertificate']), 0)
                    assert_equal(len(rpcDataByHeightVerbosity['matureCertificate']), 0)
                # Assert that for every block the list of certificates is equal to the list of MATURE certificates
                # That checks that certificates immediately mature once they are included in a block
                # (Only for v2 non-ceasable sc)
                if (rpcDataByHash['height'] in self.cert_dict_non_ceasing):
                    assert_true(self.check_equal(rpcDataByHash['matureCertificate'],               rpcDataByHash['cert']))
                    assert_true(self.check_equal(rpcDataByHeight['matureCertificate'],             rpcDataByHeight['cert']))
                    assert_true(self.check_equal(rpcDataByHashVerbosity['matureCertificate'],      rpcDataByHashVerbosity['cert']))
                    assert_true(self.check_equal(rpcDataByHeightVerbosity['matureCertificate'],    rpcDataByHeightVerbosity['cert']))
            else:
                assert_false('matureCertificate' in rpcDataByHash)
                assert_false('matureCertificate' in rpcDataByHeight)
                assert_false('matureCertificate' in rpcDataByHashVerbosity)
                assert_false('matureCertificate' in rpcDataByHeightVerbosity)

        self.nodes[0].invalidateblock(new_epoch_block)
        self.nodes[0].invalidateblock(cert3_block)
        self.nodes[1].invalidateblock(new_epoch_block)
        self.nodes[1].invalidateblock(cert3_block)
        assert_equal(self.nodes[0].getblockcount(), self.blck_h[-1])
        assert_equal(self.nodes[1].getblockcount(), self.blck_h[-1])

        self.nodes[0].clearmempool()
        self.nodes[1].clearmempool()
        assert_equal(len(self.nodes[0].getrawmempool()),0)
        assert_equal(len(self.nodes[1].getrawmempool()),0)
        self.sync_all()

        fake_block = self.nodes[0].generate(1)[0]
        self.sync_all()

        blcknum = str(int(self.blck_h[-1]) + 1)

        rpcDataByHeight = self.nodes[0].getblockexpanded(blcknum)
        rpcDataByHeightVerbosity = self.nodes[0].getblockexpanded(blcknum, 2)
        rpcDataByHash = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'])
        rpcDataByHashVerbosity = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'],2)
        assert_equal(len(rpcDataByHash['matureCertificate']), 0)
        assert_equal(len(rpcDataByHeight['matureCertificate']), 0)
        assert_equal(len(rpcDataByHashVerbosity['matureCertificate']), 0)
        assert_equal(len(rpcDataByHeightVerbosity['matureCertificate']), 0)

        self.nodes[0].invalidateblock(fake_block)
        self.nodes[1].invalidateblock(fake_block)

        self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_3, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.nodes[0].generate(2)
        self.sync_all()

        rpcDataByHeight = self.nodes[0].getblockexpanded(blcknum)
        rpcDataByHeightVerbosity = self.nodes[0].getblockexpanded(blcknum, 2)
        rpcDataByHash = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'])
        rpcDataByHashVerbosity = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'],2)
        assert_equal(len(rpcDataByHash['matureCertificate']), 1)
        assert_equal(len(rpcDataByHeight['matureCertificate']), 1)
        assert_equal(len(rpcDataByHashVerbosity['matureCertificate']), 1)
        assert_equal(len(rpcDataByHeightVerbosity['matureCertificate']), 1)
        assert_equal(rpcDataByHashVerbosity['matureCertificate'][0], cert_3_json)
        assert_equal(rpcDataByHeightVerbosity['matureCertificate'][0], cert_3_json)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['state'], "ALIVE")

        #Verify that SC does not cease
        self.nodes[0].generate(130)
        self.sync_all()

        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['state'], "ALIVE")
        tipHeightCeased = self.nodes[0].getblockcount()

        for i in range (tipHeight, tipHeightCeased+1):
            rpcDataByHeight = self.nodes[0].getblockexpanded(str(i))
            rpcDataByHeightVerbosity = self.nodes[0].getblockexpanded(str(i), 2)
            rpcDataByHash = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'])
            rpcDataByHashVerbosity = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'],2)

            assert_equal(len(rpcDataByHash['matureCertificate']), 0)
            assert_equal(len(rpcDataByHeight['matureCertificate']), 0)
            assert_equal(len(rpcDataByHashVerbosity['matureCertificate']), 0)
            assert_equal(len(rpcDataByHeightVerbosity['matureCertificate']), 0)

        self.round_number = self.round_number + 1

    def run_test(self):
        mark_logs("\n**SC version 0", self.nodes, DEBUG_MODE)
        self.run_test_ceasable(0)
        mark_logs("\n**SC version 2 - ceasable SC", self.nodes, DEBUG_MODE)
        self.run_test_ceasable(2)
        mark_logs("\n**SC version 2 - non-ceasable SC", self.nodes, DEBUG_MODE)
        self.run_test_non_ceasable(2)

if __name__ == '__main__':
    getblockexpanded().main()
