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
    get_epoch_data, assert_false, assert_true, swap_bytes
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 3
EPOCH_LENGTH = 20
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')
HIGH_CERT_FEE = Decimal('0.00020')
LOW_CERT_FEE = Decimal('0.00005')

class quality_mempool(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-scproofqueuesize=0', '-logtimemicros=1']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        The test creates two sc, send funds to them and then sends a certificates to them,
        verifying also that mempool accepts certificate with different quality,
        certificate with higher fee but same quality substitutes certificate with lower fee,
        certificates from different sc are handled independently.
        '''

        # forward transfer amounts
        creation_amount = Decimal("0.5")
        fwt_amount = Decimal("200")
        bwt_amount = Decimal("20")

        self.nodes[0].getblockhash(0)

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()

        # SC creation
        bal_before_sc_creation = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance before SC creation: {}".format(bal_before_sc_creation), self.nodes, DEBUG_MODE)

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk_tag_1 = "sc1"
        vk_1 = mcTest.generate_params(vk_tag_1)
        constant_1 = generate_random_field_element_hex()

        vk_tag_2 = "sc2"
        vk_2 = mcTest.generate_params(vk_tag_2)
        constant_2 = generate_random_field_element_hex()
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk_1,
            "constant": constant_1,
        }

        ret = self.nodes[1].sc_create(cmdInput)
        creating_tx_1 = ret['txid']
        scid_1 = ret['scid']
        scid1_swapped = str(swap_bytes(scid_1))
        mark_logs("Node 1 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx_1), self.nodes, DEBUG_MODE)
        self.sync_all()

        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "baba",
            "amount": creation_amount,
            "wCertVk": vk_2,
            "constant": constant_2,
            "minconf": 0
        }

        ret = self.nodes[1].sc_create(cmdInput)
        creating_tx_2 = ret['txid']
        scid_2 = ret['scid']
        scid2_swapped = str(swap_bytes(scid_2))
        mark_logs("Node 1 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx_2), self.nodes, DEBUG_MODE)
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(creating_tx_1, 1)
        assert_equal(scid_1, decoded_tx['vsc_ccout'][0]['scid'])
        mark_logs("created SC id: {}".format(scid_1), self.nodes, DEBUG_MODE)

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        sc_creating_height = self.nodes[0].getblockcount()
        self.sync_all()

        # Check node 1 balance following sc creation
        fee_sc_creation_1 = Decimal(self.nodes[1].gettransaction(creating_tx_1)['fee'])
        fee_sc_creation_2 = Decimal(self.nodes[1].gettransaction(creating_tx_2)['fee'])
        mark_logs("Fee paid for SC1 creation: {}".format(fee_sc_creation_1), self.nodes, DEBUG_MODE)
        mark_logs("Fee paid for SC2 creation: {}".format(fee_sc_creation_2), self.nodes, DEBUG_MODE)
        bal_after_sc_creation = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance after SC creation: {}".format(bal_after_sc_creation), self.nodes, DEBUG_MODE)

        assert_equal(bal_before_sc_creation, bal_after_sc_creation + creation_amount + creation_amount - fee_sc_creation_1 - fee_sc_creation_2)

        assert_equal(self.nodes[0].getscinfo(scid_1)['items'][0]['balance'], Decimal(0))
        assert_equal(self.nodes[0].getscinfo(scid_1)['items'][0]['immatureAmounts'][0]['amount'], creation_amount)

        # Fwd Transfer to SC 1
        bal_before_fwd_tx = self.nodes[0].getbalance("", 0)
        mc_return_address = self.nodes[0].getnewaddress()
        mark_logs("Node0 balance before fwd tx: {}".format(bal_before_fwd_tx), self.nodes, DEBUG_MODE)
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount, "scid": scid_1, 'mcReturnAddress': mc_return_address}]
        fwd_tx = self.nodes[0].sc_send(cmdInput)
        mark_logs("Node0 transfers {} coins to SC 1 with tx {}...".format(fwt_amount, fwd_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node0 confirms fwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # Fwd Transfer to SC 2
        bal_before_fwd_tx = self.nodes[0].getbalance("", 0)
        mc_return_address = self.nodes[0].getnewaddress()
        mark_logs("Node0 balance before fwd tx: {}".format(bal_before_fwd_tx), self.nodes, DEBUG_MODE)
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount, "scid": scid_2, 'mcReturnAddress': mc_return_address}]
        fwd_tx = self.nodes[0].sc_send(cmdInput)
        mark_logs("Node0 transfers {} coins to SC 2 with tx {}...".format(fwt_amount, fwd_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node0 confirms fwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # Check node 0 balance following fwd tx
        fee_fwt = self.nodes[0].gettransaction(fwd_tx)['fee']
        mark_logs("Fee paid for fwd tx: {}".format(fee_fwt), self.nodes, DEBUG_MODE)
        bal_after_fwd_tx = self.nodes[0].getbalance("", 0)
        mark_logs("Node0 balance after fwd: {}".format(bal_after_fwd_tx), self.nodes, DEBUG_MODE)
        assert_equal(bal_before_fwd_tx, bal_after_fwd_tx + fwt_amount - fee_fwt - Decimal(MINER_REWARD_POST_H200))

        assert_equal(self.nodes[0].getscinfo(scid_1)['items'][0]['balance'], Decimal(0))
        assert_equal(self.nodes[0].getscinfo(scid_1)['items'][0]['immatureAmounts'][0]['amount'], creation_amount)
        assert_equal(self.nodes[0].getscinfo(scid_1)['items'][0]['immatureAmounts'][1]['amount'], fwt_amount)

        mark_logs("Node0 generating more blocks to achieve end of withdrawal epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(EPOCH_LENGTH - 3)
        self.sync_all()
        assert_equal(self.nodes[0].getscinfo(scid_1)['items'][0]['balance'], creation_amount + fwt_amount) # Sc balance has matured
        assert_equal(len(self.nodes[0].getscinfo(scid_1)['items'][0]['immatureAmounts']), 0)

        epoch_number_1, epoch_cum_tree_hash_1, _ = get_epoch_data(scid_1, self.nodes[0], EPOCH_LENGTH)

        addr_node1 = self.nodes[1].getnewaddress()
        self.sync_all()

        epoch_number_2, epoch_cum_tree_hash_2, _ = get_epoch_data(scid_2, self.nodes[0], EPOCH_LENGTH)
        amount_cert = [{"address": addr_node1, "amount": bwt_amount}]

        # Create Cert1 with quality 100 and place it in mempool
        mark_logs("Create Cert1 with quality 100 and place it in mempool", self.nodes, DEBUG_MODE)
        quality = 100
        proof = mcTest.create_test_proof(vk_tag_1,
                                         scid1_swapped,
                                         epoch_number_1,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash_1,
                                         constant = constant_1,
                                         pks      = [addr_node1],
                                         amounts  = [bwt_amount])
        try:
            cert_1_epoch_0 = self.nodes[0].sc_send_certificate(scid_1, epoch_number_1, quality,
                epoch_cum_tree_hash_1, proof, amount_cert, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_1_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_1_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Checking mempools alignment", self.nodes, DEBUG_MODE)
        self.sync_all()
        for i in range(1, NUMB_OF_NODES):
            assert_equal(sorted(self.nodes[0].getrawmempool()), sorted(self.nodes[i].getrawmempool()))

        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_1_epoch_0 in self.nodes[0].getrawmempool())

        # Create Cert2 with lower quality, any fee, deps on cert_1 and try to place it in mempool
        mark_logs("Checking rejection of cert_2 with same (scId, epoch), lower quality,  any fee, with spending deps on cert_1", self.nodes, DEBUG_MODE)
        low_quality_proof = mcTest.create_test_proof(vk_tag_1,
                                                     scid1_swapped,
                                                     epoch_number_1,
                                                     quality - 10,
                                                     MBTR_SC_FEE,
                                                     FT_SC_FEE,
                                                     epoch_cum_tree_hash_1,
                                                     constant = constant_1,
                                                     pks      = [addr_node1],
                                                     amounts  = [bwt_amount])

        # get a UTXO
        utx = False
        listunspent = self.nodes[0].listunspent(0)
        for aUtx in listunspent:
            if aUtx['amount'] > HIGH_CERT_FEE and aUtx['txid'] == cert_1_epoch_0:
                utx = aUtx
                change = aUtx['amount'] - CERT_FEE
                break;

        inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        outputs = { self.nodes[0].getnewaddress(): change }
        bwt_outs = [{"address":addr_node1, "amount":bwt_amount}]
        params = {
            "scid": scid_1,
            "quality": quality - 10,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash_1,
            "scProof": low_quality_proof,
            "withdrawalEpochNumber": epoch_number_1
        }

        try:
            rawcert    = self.nodes[0].createrawcertificate(inputs, outputs, bwt_outs, params)
            signed_cert = self.nodes[0].signrawtransaction(rawcert)
            cert2 = self.nodes[0].sendrawtransaction(signed_cert['hex'])
            assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_1_epoch_0 in self.nodes[0].getrawmempool())

        # Create Cert2 with lower quality and place it in mempool
        mark_logs("Create Cert2 with lower quality and place it in mempool", self.nodes, DEBUG_MODE)
        addr_node2 = self.nodes[2].getnewaddress()
        amount_cert_2 = [{"address": addr_node2, "amount": bwt_amount}]
        low_quality_proof = mcTest.create_test_proof(vk_tag_1,
                                                     scid1_swapped,
                                                     epoch_number_1,
                                                     quality - 10,
                                                     MBTR_SC_FEE,
                                                     FT_SC_FEE,
                                                     epoch_cum_tree_hash_1,
                                                     constant = constant_1,
                                                     pks      = [addr_node2],
                                                     amounts  = [bwt_amount])
        try:
            cert_2_epoch_0 = self.nodes[1].sc_send_certificate(scid_1, epoch_number_1, quality - 10,
                epoch_cum_tree_hash_1, low_quality_proof, amount_cert_2, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_2_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_2_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        # Checking all certificate in mempool
        self.sync_all()
        assert_equal(True, cert_1_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(True, cert_2_epoch_0 in self.nodes[0].getrawmempool())

        # Create Cert3 with equal quality but lower fee and try to place it in mempool
        mark_logs("Checking rejection of cert_3 with same (scId, epoch), equal quality, lower fee, no spending deps on cert_1", self.nodes, DEBUG_MODE)
        addr_node2 = self.nodes[2].getnewaddress()
        amount_cert_3 = [{"address": addr_node2, "amount": bwt_amount}]
        cert3_proof = mcTest.create_test_proof(vk_tag_1,
                                               scid1_swapped,
                                               epoch_number_1,
                                               quality,
                                               MBTR_SC_FEE,
                                               FT_SC_FEE,
                                               epoch_cum_tree_hash_1,
                                               constant = constant_1,
                                               pks      = [addr_node2],
                                               amounts  = [bwt_amount])
        try:
            cert_3_epoch_0 = self.nodes[0].sc_send_certificate(scid_1, epoch_number_1, quality,
                epoch_cum_tree_hash_1, cert3_proof, amount_cert_3, FT_SC_FEE, MBTR_SC_FEE, LOW_CERT_FEE)
            assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        # Checking all certificate in mempool
        self.sync_all()
        assert_equal(True, cert_1_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(True, cert_2_epoch_0 in self.nodes[0].getrawmempool())

        # Create Cert3 with equal quality, equal fee and try to place it in mempool
        mark_logs("Checking rejection of cert_3 with same (scId, epoch), equal quality, equal fee, no spending deps on cert_1", self.nodes, DEBUG_MODE)
        amount_cert_3 = [{"address": addr_node2, "amount": bwt_amount}]
        cert3_proof = mcTest.create_test_proof(vk_tag_1,
                                               scid1_swapped,
                                               epoch_number_1,
                                               quality,
                                               MBTR_SC_FEE,
                                               FT_SC_FEE,
                                               epoch_cum_tree_hash_1,
                                               constant = constant_1,
                                               pks      = [addr_node2],
                                               amounts  = [bwt_amount])
        try:
            cert_3_epoch_0 = self.nodes[1].sc_send_certificate(scid_1, epoch_number_1, quality,
                epoch_cum_tree_hash_1, cert3_proof, amount_cert_3, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        # Checking all certificate in mempool
        self.sync_all()
        assert_equal(True, cert_1_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(True, cert_2_epoch_0 in self.nodes[0].getrawmempool())

        # Create Cert3 with equal quality, high fee and try to place it in mempool
        mark_logs("Checking rejection of cert_3 with same (scId, epoch), equal quality, higher fee, with spending deps on cert_1", self.nodes, DEBUG_MODE)
        amount_cert_3 = [{"address": addr_node1, "amount": bwt_amount}]
        cert3_proof = mcTest.create_test_proof(vk_tag_1,
                                               scid1_swapped,
                                               epoch_number_1,
                                               quality,
                                               MBTR_SC_FEE,
                                               FT_SC_FEE,
                                               epoch_cum_tree_hash_1,
                                               constant = constant_1,
                                               pks      = [addr_node1],
                                               amounts  = [bwt_amount])

        # get a UTXO
        utx = False
        listunspent = self.nodes[0].listunspent(0)
        for aUtx in listunspent:
            if aUtx['amount'] > HIGH_CERT_FEE and aUtx['txid'] == cert_1_epoch_0:
                utx = aUtx
                change = aUtx['amount'] - HIGH_CERT_FEE
                break;

        inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        outputs = { self.nodes[0].getnewaddress(): change }
        bwt_outs = [{"address":addr_node1, "amount":bwt_amount}]
        params = {
            "scid": scid_1,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash_1,
            "scProof": cert3_proof,
            "withdrawalEpochNumber": epoch_number_1
        }

        try:
            rawcert    = self.nodes[0].createrawcertificate(inputs, outputs, bwt_outs, params)
            signed_cert = self.nodes[0].signrawtransaction(rawcert)
            cert_3_epoch_0 = self.nodes[0].sendrawtransaction(signed_cert['hex'])
            assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        # Create Cert3 with quality equal to Cert1, but higher fee and place it in mempool
        addr_node3 = self.nodes[2].getnewaddress()
        amount_cert_3 = [{"address": addr_node3, "amount": bwt_amount}]

        mark_logs("Substitution of cert_3 with same (scId, epoch), equal quality,  higher fee, no spending deps on cert_1", self.nodes, DEBUG_MODE)
        normal_quality_proof = mcTest.create_test_proof(vk_tag_1,
                                                        scid1_swapped,
                                                        epoch_number_1,
                                                        quality,
                                                        MBTR_SC_FEE,
                                                        FT_SC_FEE,
                                                        epoch_cum_tree_hash_1,
                                                        constant = constant_1,
                                                        pks      = [addr_node3],
                                                        amounts  = [bwt_amount])
        try:
            cert_3_epoch_0 = self.nodes[2].sc_send_certificate(scid_1, epoch_number_1, quality,
                epoch_cum_tree_hash_1, normal_quality_proof, amount_cert_3, FT_SC_FEE, MBTR_SC_FEE, HIGH_CERT_FEE)
            mark_logs("Certificate is {}".format(cert_3_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        # Checking cert_2 and cert_3 in mempool. cert_1 must be substituted with cert_3.
        self.sync_all()
        assert_equal(False, cert_1_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(True, cert_2_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(True, cert_3_epoch_0 in self.nodes[0].getrawmempool())

        # Create Cert4 with higher quality and try to place it in mempool
        mark_logs("Check insertion of cert_4 with same (scId, epoch), higher quality, any fee, no spending deps on cert_3", self.nodes, DEBUG_MODE)
        addr_node4 = self.nodes[2].getnewaddress()
        amount_cert_4 = [{"address": addr_node4, "amount": bwt_amount}]
        high_quality_proof = mcTest.create_test_proof(vk_tag_1,
                                                      scid1_swapped,
                                                      epoch_number_1,
                                                      quality + 20,
                                                      MBTR_SC_FEE,
                                                      FT_SC_FEE,
                                                      epoch_cum_tree_hash_1,
                                                      constant = constant_1,
                                                      pks      = [addr_node4],
                                                      amounts  = [bwt_amount])
        try:
            cert_4_epoch_0 = self.nodes[2].sc_send_certificate(scid_1, epoch_number_1, quality + 20,
                epoch_cum_tree_hash_1, high_quality_proof, amount_cert_4, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            sync_mempools(self.nodes[1:3])
            assert(len(cert_4_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_4_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        # Checking all certificate in mempool
        self.sync_all()
        assert_equal(False, cert_1_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(True, cert_2_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(True, cert_3_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(True, cert_4_epoch_0 in self.nodes[0].getrawmempool())

        mark_logs("Node1 confims bwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        mined = self.nodes[1].generate(1)[0]
        self.sync_all()

        # Cert2, Cert3 and Cert4 are in blockchain, not cert1 that was not included in miner mempool
        assert_false(cert_1_epoch_0 in self.nodes[0].getblock(mined, True)['cert'])
        assert_true(cert_2_epoch_0 in self.nodes[0].getblock(mined, True)['cert'])
        assert_true(cert_3_epoch_0 in self.nodes[0].getblock(mined, True)['cert'])
        assert_true(cert_4_epoch_0 in self.nodes[0].getblock(mined, True)['cert'])

        # Checking all certificate are not in mempool
        assert_false(cert_1_epoch_0 in self.nodes[0].getrawmempool())
        assert_false(cert_2_epoch_0 in self.nodes[0].getrawmempool())
        assert_false(cert_3_epoch_0 in self.nodes[0].getrawmempool())
        assert_false(cert_4_epoch_0 in self.nodes[0].getrawmempool())

        # Checking that certificates related to different scIds are handled independently
        # Create Cert1 with quality 150 and place it in mempool
        mark_logs("Create Cert1 from SC1 with quality 150 and place it in mempool", self.nodes, DEBUG_MODE)
        addr_node1 = self.nodes[1].getnewaddress()
        amount_cert_1 = [{"address": addr_node1, "amount": bwt_amount}]
        quality = 150
        proof = mcTest.create_test_proof(vk_tag_1,
                                         scid1_swapped,
                                         epoch_number_1,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash_1,
                                         constant = constant_1,
                                         pks      = [addr_node1],
                                         amounts  = [bwt_amount])
        try:
            cert_1_sc1 = self.nodes[0].sc_send_certificate(scid_1, epoch_number_1, quality,
                epoch_cum_tree_hash_1, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_1_sc1) > 0)
            mark_logs("Certificate is {}".format(cert_1_sc1), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        # Create Cert2 with quality 100 and place it in mempool
        mark_logs("Create Cert2 from SC2 with quality 100(as in previous block in SC1) and place it in mempool", self.nodes, DEBUG_MODE)
        addr_node2 = self.nodes[2].getnewaddress()
        amount_cert_2 = [{"address": addr_node2, "amount": bwt_amount}]
        quality = 100
        proof = mcTest.create_test_proof(vk_tag_2,
                                         scid2_swapped,
                                         epoch_number_2,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash_2,
                                         constant = constant_2,
                                         pks      = [addr_node2],
                                         amounts  = [bwt_amount])
        try:
            cert_2_sc2 = self.nodes[2].sc_send_certificate(scid_2, epoch_number_2, quality,
                epoch_cum_tree_hash_2, proof, amount_cert_2, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_2_sc2) > 0)
            mark_logs("Certificate is {}".format(cert_2_sc2), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()
        assert_equal(True, cert_1_sc1 in self.nodes[0].getrawmempool())
        assert_equal(True, cert_2_sc2 in self.nodes[0].getrawmempool())

        # Create Cert3 with quality 150 and place it in mempool
        mark_logs("Create Cert3 from SC2 with quality 150(as cert1 in mempool from SC1) and place it in mempool", self.nodes, DEBUG_MODE)
        addr_node1 = self.nodes[1].getnewaddress()
        amount_cert_3 = [{"address": addr_node1, "amount": bwt_amount}]
        quality = 150
        proof = mcTest.create_test_proof(vk_tag_2,
                                         scid2_swapped,
                                         epoch_number_2,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash_2,
                                         constant = constant_2,
                                         pks      = [addr_node1],
                                         amounts  = [bwt_amount])
        try:
            cert_3_sc2 = self.nodes[1].sc_send_certificate(scid_2, epoch_number_2, quality,
                epoch_cum_tree_hash_2, proof, amount_cert_3, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_3_sc2) > 0)
            mark_logs("Certificate is {}".format(cert_3_sc2), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()
        assert_equal(True, cert_1_sc1 in self.nodes[0].getrawmempool())
        assert_equal(True, cert_2_sc2 in self.nodes[0].getrawmempool())
        assert_equal(True, cert_3_sc2 in self.nodes[0].getrawmempool())

        mark_logs("Node1 confims bwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        mined = self.nodes[1].generate(1)[0]
        self.sync_all()

        # Checking certificates in blockchain
        assert_true(cert_1_sc1 in self.nodes[0].getblock(mined, True)['cert'])
        assert_true(cert_2_sc2 in self.nodes[0].getblock(mined, True)['cert'])
        assert_true(cert_3_sc2 in self.nodes[0].getblock(mined, True)['cert'])

        assert_equal(False, cert_1_sc1 in self.nodes[0].getrawmempool())
        assert_equal(False, cert_2_sc2 in self.nodes[0].getrawmempool())
        assert_equal(False, cert_3_sc2 in self.nodes[0].getrawmempool())

        # Cert6 depends on Cert5(quality of Cert6 > quality of Cert5). Check that Cert7 will be rejected on attempt to replace Cert1
        # Create Cert5 with quality 200 and place it in mempool
        mark_logs("Create Cert5 with quality 200 and place it in mempool", self.nodes, DEBUG_MODE)
        quality = 200
        amount_cert_1 = [{"address": addr_node1, "amount": bwt_amount}]
        quality_proof = mcTest.create_test_proof(vk_tag_1,
                                                 scid1_swapped,
                                                 epoch_number_1,
                                                 quality,
                                                 MBTR_SC_FEE,
                                                 FT_SC_FEE,
                                                 epoch_cum_tree_hash_1,
                                                 constant = constant_1,
                                                 pks      = [addr_node1],
                                                 amounts  = [bwt_amount])
        try:
            cert_5_epoch_0 = self.nodes[0].sc_send_certificate(scid_1, epoch_number_1, quality,
                epoch_cum_tree_hash_1, quality_proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_5_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_5_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        self.sync_all()
        assert_true(cert_5_epoch_0 in self.nodes[0].getrawmempool())

        mark_logs("Create Cert6 with quality 220, with dependency on Cert5 and place it in mempool", self.nodes, DEBUG_MODE)
        quality = 220
        amount_cert_6 = [{"address": addr_node1, "amount": bwt_amount}]
        cert6_proof = mcTest.create_test_proof(vk_tag_1,
                                               scid1_swapped,
                                               epoch_number_1,
                                               quality,
                                               MBTR_SC_FEE,
                                               FT_SC_FEE,
                                               epoch_cum_tree_hash_1,
                                               constant = constant_1,
                                               pks      = [addr_node1],
                                               amounts  = [bwt_amount])

        # get a UTXO
        utx = False
        listunspent = self.nodes[0].listunspent(0)
        for aUtx in listunspent:
            if aUtx['amount'] > HIGH_CERT_FEE and aUtx['txid'] == cert_5_epoch_0:
                utx = aUtx
                change = aUtx['amount'] - CERT_FEE
                break;

        inputs = [{'txid': utx['txid'], 'vout': utx['vout']}]
        outputs = {self.nodes[0].getnewaddress(): change}
        bwt_outs = [{"address":addr_node1, "amount":bwt_amount}]
        params = {
            "scid": scid_1,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash_1,
            "scProof": cert6_proof,
            "withdrawalEpochNumber": epoch_number_1
        }

        try:
            rawcert = self.nodes[0].createrawcertificate(inputs, outputs, bwt_outs, params)
            signed_cert = self.nodes[0].signrawtransaction(rawcert)
            cert_6_epoch_0 = self.nodes[0].sendrawtransaction(signed_cert['hex'])
            mark_logs("Certificate is {}".format(cert_6_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        self.sync_all()
        assert_true(cert_5_epoch_0 in self.nodes[0].getrawmempool())
        assert_true(cert_6_epoch_0 in self.nodes[0].getrawmempool())

        # Create Cert7 with quality 200 and place it in mempool
        mark_logs("Check rejection Cert7 that tries to replace Cert5", self.nodes, DEBUG_MODE)
        quality = 200
        addr_node2 = self.nodes[2].getnewaddress()
        amount_cert_2 = [{"address": addr_node2, "amount": bwt_amount}]
        quality_proof = mcTest.create_test_proof(vk_tag_1,
                                                 scid1_swapped,
                                                 epoch_number_1,
                                                 quality,
                                                 MBTR_SC_FEE,
                                                 FT_SC_FEE,
                                                 epoch_cum_tree_hash_1,
                                                 constant = constant_1,
                                                 pks      = [addr_node2],
                                                 amounts  = [bwt_amount])
        try:
            cert_7_epoch_0 = self.nodes[2].sc_send_certificate(scid_1, epoch_number_1, quality,
                epoch_cum_tree_hash_1, quality_proof, amount_cert_2, FT_SC_FEE, MBTR_SC_FEE, HIGH_CERT_FEE)
            assert(len(cert_7_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_7_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)


        self.sync_all()
        assert_false(cert_5_epoch_0 in self.nodes[0].getrawmempool())
        assert_false(cert_6_epoch_0 in self.nodes[0].getrawmempool())
        assert_true(cert_7_epoch_0 in self.nodes[0].getrawmempool())

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)

if __name__ == '__main__':
    quality_mempool().main()
