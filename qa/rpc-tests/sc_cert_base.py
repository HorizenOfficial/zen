#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs,\
    get_epoch_data, assert_false, assert_true, swap_bytes, COIN
from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.mc_test.mc_test import *
import os
import pprint
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 3
EPOCH_LENGTH = 17
#EPOCH_LENGTH = 5
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
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
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib', '-scproofqueuesize=0', '-logtimemicros=1']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        The test creates a sc, send funds to it and then sends a certificate to it,
        verifying also that specifying various combination of bad parameters causes a certificate
        to be refused. This test also checks that the receiver of cert backward transfer can spend it
        only when they become mature. 
        '''

        # forward transfer amounts
        creation_amount = Decimal("0.5")
        fwt_amount = Decimal("50")
        bwt_amount_bad = Decimal("100.0")
        bwt_amount = Decimal("50")
        bwt_amount_0_b = Decimal("0.10")

        self.nodes[0].getblockhash(0)

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()

        # SC creation
        bal_before_sc_creation = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance before SC creation: {}".format(bal_before_sc_creation), self.nodes, DEBUG_MODE)

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1", "cert")
        constant = generate_random_field_element_hex()
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength":EPOCH_LENGTH,
            "toaddress":"dada",
            "amount":creation_amount,
            "wCertVk":vk,
            "constant":constant
        }

        ret = self.nodes[1].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs("Node 1 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        sc_creating_height = self.nodes[0].getblockcount()
        self.sync_all()

        # Check node 1 balance following sc creation
        fee_sc_creation = self.nodes[1].gettransaction(creating_tx)['fee']
        mark_logs("Fee paid for SC creation: {}".format(fee_sc_creation), self.nodes, DEBUG_MODE)
        bal_after_sc_creation = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance after SC creation: {}".format(bal_after_sc_creation), self.nodes, DEBUG_MODE)
        assert_equal(bal_before_sc_creation, bal_after_sc_creation + creation_amount - fee_sc_creation)

        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], Decimal(0))
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts'][0]['amount'], creation_amount)

        # Fwd Transfer to Sc
        bal_before_fwd_tx = self.nodes[0].getbalance("", 0)
        mark_logs("Node0 balance before fwd tx: {}".format(bal_before_fwd_tx), self.nodes, DEBUG_MODE)
        mc_return_address = self.nodes[0].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount, "scid": scid, "mcReturnAddress": mc_return_address}]
        fwd_tx = self.nodes[0].sc_send(cmdInput)
        mark_logs("Node0 transfers {} coins to SC with tx {}...".format(fwt_amount, fwd_tx), self.nodes, DEBUG_MODE)
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

        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], Decimal(0))
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts'][0]['amount'], creation_amount)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts'][1]['amount'], fwt_amount)

        nblocks = EPOCH_LENGTH - 2
        mark_logs("Node0 generating {} more blocks to achieve end of withdrawal epoch".format(nblocks), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(nblocks)
        self.sync_all()
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc balance has matured
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)

        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        addr_node1 = self.nodes[1].getnewaddress()

        #Create proof for WCert
        quality = 10
        proof = mcTest.create_test_proof("sc1",
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         prev_cert_hash = None,
                                         constant       = constant,
                                         pks            = [addr_node1],
                                         amounts        = [bwt_amount])

        amount_cert_1 = [{"address": addr_node1, "amount": bwt_amount}]

        #---------------------start negative tests-------------------------

        mark_logs("Node 0 tries to send a cert with insufficient Sc balance...", self.nodes, DEBUG_MODE)
        amounts = [{"address": addr_node1, "amount": bwt_amount_bad}]

        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("sidechain has insufficient funds" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount)
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)
        #--------------------------------------------------------------------------------------

        mark_logs("Node 0 tries to send a certificate with an invalid epoch number ...", self.nodes, DEBUG_MODE)

        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number + 1, quality,
                epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        #TODO: Check that, even if we passed an invalid epoch_number but a valid epoch_cum_tree_hash,
        #      This error message is expected; e.g. the send certificate fails for the correct reason
        assert_equal("invalid end cum commitment tree root" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)
        #--------------------------------------------------------------------------------------

        mark_logs("Node 0 tries to send a certificate with an invalid epoch epoch_cum_tree_hash ...", self.nodes, DEBUG_MODE)

        wrong_epoch_cum_tree_hash = generate_random_field_element_hex()
        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                wrong_epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("invalid end cum commitment tree root" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)
        #--------------------------------------------------------------------------------------

        mark_logs("Node 0 tries to send a certificate with an invalid quality ...", self.nodes, DEBUG_MODE)

        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality - 100,
                epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("Invalid quality parameter" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)
        #--------------------------------------------------------------------------------------
        
        mark_logs("Node 0 tries to send a certificate with a scProof too long ...", self.nodes, DEBUG_MODE)

        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, "aa" * (MAX_SC_PROOF_SIZE_IN_BYTES + 1), amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("scProof: Invalid length" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)

        #--------------------------------------------------------------------------------------

        mark_logs("Node 0 tries to send a certificate with an invalid scProof ...", self.nodes, DEBUG_MODE)

        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, "aa", amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("invalid cert scProof" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)

        #--------------------------------------------------------------------------------------

        mark_logs("Node 0 tries to send a certificate with a scProof including a bwt with wrong amount...", self.nodes, DEBUG_MODE)

        #Create wrong proof for WCert
        proof_wrong = mcTest.create_test_proof("sc1",
                                               scid_swapped,
                                               epoch_number,
                                               quality,
                                               MBTR_SC_FEE,
                                               FT_SC_FEE,
                                               epoch_cum_tree_hash,
                                               prev_cert_hash = None,
                                               constant       = constant,
                                               pks            = [addr_node1],
                                               amounts        = [bwt_amount_bad])
        
        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof_wrong, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("bad-sc-cert-proof" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)
        
        #--------------------------------------------------------------------------------------

        mark_logs("Node 0 tries to send a certificate with a scProof including a bwt with wrong dest pk...", self.nodes, DEBUG_MODE)

        #Create wrong proof for WCert
        addr_node1_bad = self.nodes[1].getnewaddress()
        proof_wrong = mcTest.create_test_proof("sc1",
                                               scid_swapped,
                                               epoch_number,
                                               quality,
                                               MBTR_SC_FEE,
                                               FT_SC_FEE,
                                               epoch_cum_tree_hash,
                                               prev_cert_hash = None,
                                               constant       = constant,
                                               pks            = [addr_node1_bad],
                                               amounts        = [bwt_amount])
        
        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof_wrong, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("bad-sc-cert-proof" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)

        #--------------------------------------------------------------------------------------

        mark_logs("Node 0 tries to send a certificate with a scProof containing wrong epoch_cum_tree_hash...", self.nodes, DEBUG_MODE)
        
        #Create wrong proof for WCert
        proof_wrong = mcTest.create_test_proof("sc1",
                                               scid_swapped,
                                               epoch_number,
                                               quality,
                                               MBTR_SC_FEE,
                                               FT_SC_FEE,
                                               wrong_epoch_cum_tree_hash,
                                               prev_cert_hash = None,
                                               constant       = constant,
                                               pks            = [addr_node1],
                                               amounts        = [bwt_amount])
        
        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof_wrong, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("bad-sc-cert-proof" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)

        #--------------------------------------------------------------------------------------

        mark_logs("Node 0 tries to send a certificate with a scProof containing wrong quality...", self.nodes, DEBUG_MODE)

        #Create wrong proof for WCert
        proof_wrong = mcTest.create_test_proof("sc1",
                                               scid_swapped,
                                               epoch_number,
                                               quality + 1,
                                               MBTR_SC_FEE,
                                               FT_SC_FEE,
                                               epoch_cum_tree_hash,
                                               prev_cert_hash = None,
                                               constant       = constant,
                                               pks            = [addr_node1],
                                               amounts        = [bwt_amount])

        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof_wrong, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("bad-sc-cert-proof" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)

        #--------------------------------------------------------------------------------------

        mark_logs("Node 0 tries to send a certificate with a scProof containing wrong MBTR_SC_FEE...", self.nodes, DEBUG_MODE)

        #Create wrong proof for WCert
        proof_wrong = mcTest.create_test_proof("sc1",
                                               scid_swapped,
                                               epoch_number,
                                               quality,
                                               MBTR_SC_FEE + 1,
                                               FT_SC_FEE,
                                               epoch_cum_tree_hash,
                                               prev_cert_hash = None,
                                               constant       = constant,
                                               pks            = [addr_node1],
                                               amounts        = [bwt_amount])
        
        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof_wrong, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("bad-sc-cert-proof" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)

        #--------------------------------------------------------------------------------------

        mark_logs("Node 0 tries to send a certificate with a scProof containing wrong FT_SC_FEE...", self.nodes, DEBUG_MODE)

        #Create wrong proof for WCert
        proof_wrong = mcTest.create_test_proof("sc1",
                                               scid_swapped,
                                               epoch_number,
                                               quality,
                                               MBTR_SC_FEE,
                                               FT_SC_FEE + 1,
                                               epoch_cum_tree_hash,
                                               prev_cert_hash = None,
                                               constant       = constant,
                                               pks            = [addr_node1],
                                               amounts        = [bwt_amount])

        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof_wrong, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("bad-sc-cert-proof" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)

        #--------------------------------------------------------------------------------------

        mark_logs("Node 0 tries to send a certificate with a scProof containing wrong constant...", self.nodes, DEBUG_MODE)

        #Create wrong proof for WCert
        constant_bad = generate_random_field_element_hex()
        proof_wrong = mcTest.create_test_proof("sc1",
                                               scid_swapped,
                                               epoch_number,
                                               quality,
                                               MBTR_SC_FEE,
                                               FT_SC_FEE,
                                               epoch_cum_tree_hash,
                                               prev_cert_hash = None,
                                               constant       = constant_bad,
                                               pks            = [addr_node1],
                                               amounts        = [bwt_amount])

        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof_wrong, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("bad-sc-cert-proof" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)

        #--------------------------------------------------------------------------------------

        mark_logs("Node 0 tries to send a certificate using a wrong vk for the scProof...", self.nodes, DEBUG_MODE)
        
        # let's simulate this by sending a csw proof instead of a cert one
        tempCswMcTest = CSWTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = tempCswMcTest.generate_params("sc_temp")
        amount = random.randint(0, 1000)
        sc_id = generate_random_field_element_hex()
        nullifier = generate_random_field_element_hex()
        end_cum_comm_tree_root = generate_random_field_element_hex()
        cert_data_hash = generate_random_field_element_hex()
        const = generate_random_field_element_hex()

        wrong_proof = tempCswMcTest.create_test_proof("sc_temp",
                                                      amount,
                                                      sc_id,
                                                      nullifier,
                                                      addr_node1,
                                                      end_cum_comm_tree_root,
                                                      cert_data_hash = cert_data_hash,
                                                      constant       = const)

        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, wrong_proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("bad-sc-cert-proof" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)

        #---------------------end negative tests-------------------------

        cur_h = self.nodes[0].getblockcount()

        ret=self.nodes[0].getscinfo(scid, True, False)['items'][0]
        cr_height=ret['createdAtBlockHeight']
        ceas_h = ret['ceasingHeight']
        ceas_limit_delta = ceas_h - cur_h -1

        mark_logs("epoch number={}, current height={}, creation height={}, ceasingHeight={}, ceas_limit_delta={}, epoch_len={}"
            .format(epoch_number, cur_h, cr_height, ceas_h, ceas_limit_delta, EPOCH_LENGTH), self.nodes, DEBUG_MODE)
        print

        mark_logs("Node0 generating {} blocks reaching the ceasing limit".format(ceas_limit_delta), self.nodes, DEBUG_MODE)
        mined = self.nodes[0].generate(ceas_limit_delta)[0]

        mark_logs("Node 0 sends a certificate of {} coins to Node1 address".format(amount_cert_1[0]["address"], amount_cert_1[0]["amount"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_0 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Checking mempools alignment", self.nodes, DEBUG_MODE)
        self.sync_all()
        for i in range(1, NUMB_OF_NODES):
            assert_equal(sorted(self.nodes[0].getrawmempool()), sorted(self.nodes[i].getrawmempool()))

        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_epoch_0 in self.nodes[0].getrawmempool())
        mempool_cert_0 = self.nodes[0].getrawmempool(True)[cert_epoch_0]
        assert_equal(True, mempool_cert_0["isCert"])
        assert_equal(-5, mempool_cert_0["version"])

        # change mining algo priority and fee, just for testing the api
        prio_delta = Decimal(100000.0)
        fee_delta  = 1000 # in zats
        ret = self.nodes[0].prioritisetransaction(cert_epoch_0, prio_delta, fee_delta )
        assert_equal(True, ret)

        mp = self.nodes[0].getrawmempool(True)
        #pprint.pprint(mp)
        # the prioritisetransaction cmd overwrites old json contents (not only prio and certs) 
        prio_cert_after = mp[cert_epoch_0]['priority']
        fee_cert_after  = mp[cert_epoch_0]['fee']
        mark_logs("cert prio={}, fee={}".format(prio_cert_after, fee_cert_after), self.nodes, DEBUG_MODE)
        assert_equal(prio_delta, prio_cert_after)
        assert_equal(float(fee_delta)/COIN, float(fee_cert_after))

        bal_before_bwt = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance before bwt is received: {}".format(bal_before_bwt), self.nodes, DEBUG_MODE)

        # Get BT txout excluding immature outputs
        utx_out = self.nodes[0].gettxout(cert_epoch_0, 1)
        assert_true(utx_out is None)

        include_immature = True
        # Get BT txout including immature outputs
        utx_out = self.nodes[0].gettxout(cert_epoch_0, 1, True, include_immature)
        assert_equal(utx_out["mature"], False)
        assert_equal(utx_out["maturityHeight"], -1)
        assert_equal(utx_out["blocksToMaturity"], -1)

        mark_logs("Node0 confirms certificate generating 1 block", self.nodes, DEBUG_MODE)
        mined = self.nodes[0].generate(1)[0]
        self.sync_all()

        # Get BT txout excluding mempool
        # Check that BT is immature
        utx_out = self.nodes[0].gettxout(cert_epoch_0, 1, False, include_immature)
        cur_h = self.nodes[0].getblockcount()
        cert_epoch_0_maturity_h = self.nodes[0].getscinfo(scid, True, False)['items'][0]['ceasingHeight']
        cert_epoch_0_maturity_delta = cert_epoch_0_maturity_h - cur_h - 1
        assert_equal(utx_out["mature"], False)
        assert_equal(utx_out["maturityHeight"], cert_epoch_0_maturity_h)
        assert_equal(utx_out["blocksToMaturity"], cert_epoch_0_maturity_delta)


        mark_logs("Check cert is not in mempool anymore", self.nodes, DEBUG_MODE)
        assert_equal(False, cert_epoch_0 in self.nodes[0].getrawmempool())

        mark_logs("Node 0 try to generate a certificate for the same epoch number out of the submission window", self.nodes, DEBUG_MODE)
        quality = 11
        proof2 = mcTest.create_test_proof("sc1",
                                          scid_swapped,
                                          epoch_number,
                                          quality,
                                          MBTR_SC_FEE,
                                          FT_SC_FEE,
                                          epoch_cum_tree_hash,
                                          prev_cert_hash = None,
                                          constant = constant,
                                          pks = [addr_node1],
                                          amounts = [bwt_amount])

        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof2, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("invalid timing for certificate" in errorString, True)
        self.sync_all()
        
        mark_logs("Check block coinbase contains the certificate fee", self.nodes, DEBUG_MODE)
        coinbase = self.nodes[0].getblock(mined, True)['tx'][0]
        decoded_coinbase = self.nodes[2].getrawtransaction(coinbase, 1)
        miner_quota = decoded_coinbase['vout'][0]['value']
        assert_equal(miner_quota, (Decimal(MINER_REWARD_POST_H200) + CERT_FEE))
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount- amount_cert_1[0]["amount"])
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)

        mark_logs("Checking that amount transferred by epoch 0 certificate is not mature", self.nodes, DEBUG_MODE)
        retrieved_cert = self.nodes[1].gettransaction(cert_epoch_0)
        assert_equal(retrieved_cert['amount'], 0)  # Certificate amount is not mature yet
        assert_equal(len(retrieved_cert['details']), 0)  # Certificate immature outputs should not present

        assert_equal(self.nodes[1].getwalletinfo()['immature_balance'], amount_cert_1[0]["amount"])
        utxos_Node1 = self.nodes[1].listunspent()
        for utxo in utxos_Node1:
            assert_false("cert" in utxo.keys())
            assert_false(utxo["txid"] == cert_epoch_0)

        bal_after_bwt_confirmed = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance after bwt is confirmed: {}".format(bal_after_bwt_confirmed), self.nodes, DEBUG_MODE)
        assert_equal(bal_after_bwt_confirmed, bal_before_bwt)  # cert_net_amount is not matured yet.

        mark_logs("Checking that Node1 cannot immediately spend coins received from bwd transfer", self.nodes, DEBUG_MODE)
        mark_logs("Node 1 tries to send {} coins to node2...".format(amount_cert_1[0]["amount"] / 2), self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[1].sendtoaddress(self.nodes[2].getnewaddress(), amount_cert_1[0]["amount"] / 2)
            assert(len(tx) == 0)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("Insufficient funds" in errorString, True)

        cur_h = self.nodes[0].getblockcount()
        ret=self.nodes[0].getscinfo(scid, True, False)['items'][0]
        next_ep_h = ret['endEpochHeight']
        next_ep_delta = next_ep_h - cur_h

        mark_logs("Show that coins from bwt can be spent once next epoch certificate is received and confirmed", self.nodes, DEBUG_MODE)
        mark_logs("Node0 generating {} blocks to move to new withdrawal epoch".format(next_ep_delta), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(next_ep_delta)
        self.sync_all()

        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        amount_cert_2 = []

        bal_before_cert_2 = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance before epoch 1 certificate is received: {}".format(bal_before_cert_2), self.nodes, DEBUG_MODE)        

        mark_logs("Generate new certificate for epoch {}. No bwt and no fee are included".format(epoch_number), self.nodes, DEBUG_MODE)

        # Create new proof for WCert
        quality = 1
        proof = mcTest.create_test_proof("sc1",
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         prev_cert_hash = None,
                                         constant       = constant,
                                         pks            = [],
                                         amounts        = [])

        nullFee = Decimal("0.0")
        try:
            cert_epoch_1 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_2, FT_SC_FEE, MBTR_SC_FEE, nullFee)
            assert(len(cert_epoch_1) > 0)
            mark_logs("Certificate is {}".format(cert_epoch_1), self.nodes, DEBUG_MODE)
            self.sync_all()
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        try:
            ret = self.nodes[0].getrawtransaction(cert_epoch_1, 1)
            assert_equal(ret['cert']['scid'], scid)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("can not get raw info for cert {} error: {}".format(cert_epoch_1, errorString), self.nodes, DEBUG_MODE)
    
        mark_logs("Confirm the certificate for epoch {}".format(epoch_number), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        ret=self.nodes[0].getscinfo(scid, True, False)['items'][0]
        cr_height=ret['createdAtBlockHeight']

        epoch_number_0        = epoch_number
        epoch_cum_tree_hash_0 = epoch_cum_tree_hash
        cur_h = self.nodes[0].getblockcount()
        ceas_h = ret['ceasingHeight']
        delta = ceas_h - cur_h - 1 - EPOCH_LENGTH

        mark_logs("epoch number={}, current height={}, creation height={}, ceasingHeight={}, delta={}, epoch_len={}"
            .format(epoch_number, cur_h, cr_height, ceas_h, delta, EPOCH_LENGTH), self.nodes, DEBUG_MODE)
        print

        mark_logs("Node0 generating {} blocks reaching the submission window limit".format(delta), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(delta)
        self.sync_all()

        mark_logs("Checking that amount transferred by epoch 0 certificate is not mature yet", self.nodes, DEBUG_MODE)
        retrieved_cert = self.nodes[1].gettransaction(cert_epoch_0)
        assert_equal(retrieved_cert['amount'], 0)  # Certificate amount is not mature yet

        mark_logs("Node0 generating 1 block crossing the submission window limit", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        bal_after_cert_2 = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance after epoch 1 certificate is received and submission window passed: {}".format(bal_after_cert_2), self.nodes, DEBUG_MODE)        

        mark_logs("Checking that certificate received from previous epoch 0 is now spendable,".format(epoch_number), self.nodes, DEBUG_MODE)
        retrieved_cert = self.nodes[1].gettransaction(cert_epoch_0)
        assert_equal(retrieved_cert['amount'], amount_cert_1[0]["amount"])  # Certificate amount has matured
        assert_equal(retrieved_cert['details'][0]['category'], "receive")
        assert_equal(retrieved_cert['details'][0]['amount'], amount_cert_1[0]["amount"])  # In cert details you can see the actual amount transferred

        assert_equal(self.nodes[1].getwalletinfo()['immature_balance'], Decimal(0))
        utxos_Node1 = self.nodes[1].listunspent()
        cert_epoch_0_availalble = False
        for utxo in utxos_Node1:
            if utxo["isCert"]:
                cert_epoch_0_availalble = True
                assert_true(utxo["txid"] == cert_epoch_0 )
        assert_true(cert_epoch_0_availalble)

        mark_logs("Checking Node1 balance is duly updated,".format(epoch_number), self.nodes, DEBUG_MODE)
        assert_equal(bal_after_cert_2, bal_before_cert_2 + amount_cert_1[0]["amount"])

        # Get BT txout excluding mempool
        # Check that BT is mature
        include_immature = False
        utx_out = self.nodes[0].gettxout(cert_epoch_0, 1, False, include_immature)
        assert_equal(utx_out["mature"], True)
        assert_equal(utx_out["maturityHeight"], cert_epoch_0_maturity_h)
        assert_equal(utx_out["blocksToMaturity"], 0)

        Node2_bal_before_cert_expenditure = self.nodes[2].getbalance("", 0)
        mark_logs("Checking that Node1 can spend coins received from bwd transfer in previous epoch", self.nodes, DEBUG_MODE)
        mark_logs("Node 1 sends {} coins to node2...".format(amount_cert_1[0]["amount"] / 2), self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[1].sendtoaddress(self.nodes[2].getnewaddress(), amount_cert_1[0]["amount"] / 2)
            assert(len(tx) > 0)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("tx spending certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        vin = self.nodes[1].getrawtransaction(tx, 1)['vin']
        assert_equal(vin[0]['txid'], cert_epoch_0)

        mark_logs("Node0 confims spending of bwd transfer founds generating 1 block", self.nodes, DEBUG_MODE)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        Node2_bal_after_cert_expenditure = self.nodes[2].getbalance("", 0)

        mark_logs("Verify balances following Node1 spending bwd transfer to Node2.", self.nodes, DEBUG_MODE)
        assert_equal(Node2_bal_before_cert_expenditure + amount_cert_1[0]["amount"] / 2, Node2_bal_after_cert_expenditure)

        mark_logs("Node 0 tries to send a certificate for old epoch {}...".format(epoch_number_0), self.nodes, DEBUG_MODE)
        amounts = []
        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number_0, 0,
                epoch_cum_tree_hash_0, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
        assert_equal("invalid timing for certificate" in errorString, True)

        mark_logs("Default proof constant test", self.nodes, DEBUG_MODE)
        mark_logs("Node0 creates new sidechain", self.nodes, DEBUG_MODE)
        vk2 = mcTest.generate_params("sc2", "cert", constant = None)
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength":EPOCH_LENGTH,
            "toaddress":"dada",
            "amount":creation_amount,
            "wCertVk":vk2
        }

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid2 = ret['scid']
        scid2_swapped = str(swap_bytes(scid2))
        mark_logs("Node 1 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        decoded_tx = self.nodes[0].getrawtransaction(creating_tx, 1)
        assert_equal(scid2, decoded_tx['vsc_ccout'][0]['scid'])
        mark_logs("created SC id: {}".format(scid2), self.nodes, DEBUG_MODE)

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        self.nodes[0].generate(EPOCH_LENGTH)

        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid2, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        addr_node1 = self.nodes[1].getnewaddress()

        #Create proof for WCert
        quality = 10
        proof = mcTest.create_test_proof("sc2",
                                         scid2_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         prev_cert_hash = None)

        mark_logs("Node 0 tries to send a cert with insufficient Sc balance...", self.nodes, DEBUG_MODE)
        amounts = [{"address": addr_node1, "amount": bwt_amount_bad}]

        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        self.nodes[0].generate(1)




if __name__ == '__main__':
    sc_cert_base().main()
