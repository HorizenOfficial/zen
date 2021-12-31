#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_node, connect_nodes_bi, assert_true, assert_false, get_epoch_data, \
    swap_bytes
from test_framework.mc_test.mc_test import CertTestUtils, generate_random_field_element_hex
from test_framework.authproxy import JSONRPCException

from decimal import Decimal

EPOCH_LENGTH = 100
CERT_FEE = Decimal("0.000123")
SC_FEE = Decimal("0.000345")
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
MINIMAL_SC_HEIGHT = 420

class getblockexpanded(BitcoinTestFramework):
    FEE = 0.0001
    FT_SC_FEE = Decimal('0')
    MBTR_SC_FEE = Decimal('0')
    CERT_FEE = Decimal('0.00015')

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 2)

    def setup_network(self, split=False):
        self.nodes=[]
        self.nodes += [start_node(0, self.options.tmpdir,extra_args=['-txindex=1','-maturityheightindex=1'])]
        self.nodes += [start_node(1, self.options.tmpdir)]

        connect_nodes_bi(self.nodes,0,1)

        self.is_network_split=False
        self.sync_all()

    def run_test(self):

        #amounts
        creation_amount = Decimal("50")
        bwt_amount = Decimal("5")
        tAddr1 = self.nodes[1].getnewaddress()
        node1Addr = self.nodes[1].validateaddress(tAddr1)['address']
        self.nodes[0].generate(MINIMAL_SC_HEIGHT)
        self.sync_all()
        
        ########### Create the sidechain ##################
        print("########### Create the sidechain ##################")
        
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()

        cmdInput = {
            "version": 0,
            'withdrawalEpochLength': EPOCH_LENGTH,
            'toaddress': "dada",
            'amount': creation_amount,
            'wCertVk': vk,
            'constant': constant
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
        print("########### Mine Certificate 1 with quality = 5 ##################")

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        quality = 5
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount])

        amount_cert_1 = [{"address": node1Addr, "amount": bwt_amount}]

        self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.sync_all()
        maturityHeight = sc_creation_block["height"]+(EPOCH_LENGTH*2)+EPOCH_LENGTH*0.2 - 1

        #Add to mempool Certificate 2 with quality = 7
        print("########### Add to mempool Certificate 2 with quality = 7 ##################")
        quality = 7  
        bwt_amount2 = Decimal("7")      
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount2])

        amount_cert_2 = [{"address": node1Addr, "amount": bwt_amount2}]

        self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_2, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.sync_all()

        #Mine a block
        self.nodes[0].generate(1)
        self.sync_all()

        #Mine a block with a new Certificate 3 with quality = 8
        print("########### Mine a block with a new Certificate 3 with quality = 8 ##################")
        quality = 8
        bwt_amount3 = Decimal("7")      
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount3])

        amount_cert_3 = [{"address": node1Addr, "amount": bwt_amount3}]

        cert3 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_3, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.sync_all()
        cert3_block = self.nodes[0].generate(1)[0]
        self.sync_all()

        #Advance of 1 epoch
        print("########### Advance of 1 epoch ##################")
        self.nodes[0].generate(116)
        self.sync_all()

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        quality = 9
        bwt_amount4 = Decimal("9")
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount4])

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

        #Test that we require -maturityheightindex=1 to run the getblockexpanded
        try:
            self.nodes[1].getblockexpanded("640")
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)
            assert("maturityHeightIndex option not set: can not retrieve info" in errorString)

        #Test that we see the certificate 3 but non the certificate 2 and 1
        for i in range (1,tipHeight+1):
            rpcDataByHeight = self.nodes[0].getblockexpanded(str(i))
            rpcDataByHeightVerbosity = self.nodes[0].getblockexpanded(str(i), 2)
            rpcDataByHash = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'])
            rpcDataByHashVerbosity = self.nodes[0].getblockexpanded(rpcDataByHeight['hash'],2)
         
            if (rpcDataByHash['height'] >= MINIMAL_SC_HEIGHT):
                assert_true('matureCertificate' in rpcDataByHash)
                assert_true('matureCertificate' in rpcDataByHeight)
                assert_true('matureCertificate' in rpcDataByHashVerbosity)
                assert_true('matureCertificate' in rpcDataByHeightVerbosity)
                if (rpcDataByHash['height'] == int(maturityHeight)):
                    assert_equal(len(rpcDataByHash['matureCertificate']), 1)
                    assert_equal(len(rpcDataByHeight['matureCertificate']), 1)
                    assert_equal(rpcDataByHash['matureCertificate'][0], cert3)
                    assert_equal(rpcDataByHeight['matureCertificate'][0], cert3)
                    assert_equal(len(rpcDataByHashVerbosity['matureCertificate']), 1)
                    assert_equal(len(rpcDataByHeightVerbosity['matureCertificate']), 1)
                    assert_equal(rpcDataByHashVerbosity['matureCertificate'][0], cert_3_json)
                    assert_equal(rpcDataByHeightVerbosity['matureCertificate'][0], cert_3_json)
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
        assert_equal(self.nodes[0].getblockcount(), 639)
        assert_equal(self.nodes[1].getblockcount(), 639)

        self.nodes[0].clearmempool()
        self.nodes[1].clearmempool()
        assert_equal(len(self.nodes[0].getrawmempool()),0)
        assert_equal(len(self.nodes[1].getrawmempool()),0)
        self.sync_all()

        fake_block = self.nodes[0].generate(1)[0]
        self.sync_all()

        rpcDataByHeight = self.nodes[0].getblockexpanded("640")
        rpcDataByHeightVerbosity = self.nodes[0].getblockexpanded("640", 2)
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

        rpcDataByHeight = self.nodes[0].getblockexpanded("640")
        rpcDataByHeightVerbosity = self.nodes[0].getblockexpanded("640", 2)
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

if __name__ == '__main__':
    getblockexpanded().main()
