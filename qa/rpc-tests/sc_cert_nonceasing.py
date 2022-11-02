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
import time
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 3
EPOCH_LENGTH = 0 # this test is for non ceasing sc, hence EPOCH_LENGTH must be 0!
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')
PARAMS_NAME = "sc"


class ncsc_cert_epochs(BitcoinTestFramework):

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
        self.sync_all()

    def try_send_certificate(self, node_idx, scid, epoch_number, quality, ref_height, mbtr_fee, ft_fee, bt, expect_failure, failure_reason=None):
        scid_swapped = str(swap_bytes(scid))
        ep, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[node_idx], 0, True, ref_height)
        proof = self.mcTest.create_test_proof(PARAMS_NAME,
                                              scid_swapped,
                                              epoch_number,
                                              quality,
                                              mbtr_fee,
                                              ft_fee,
                                              epoch_cum_tree_hash,
                                              prev_cert_hash,
                                              constant = self.constant,
                                              pks      = [bt["address"]],
                                              amounts  = [bt["amount"]])

        mark_logs("Node {} sends cert of quality {} epoch {} ref {} with bwt of {}, expecting {}".format(node_idx, quality, epoch_number, ref_height, bt["amount"], "failure" if expect_failure else "success"), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[node_idx].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, [bt], ft_fee, mbtr_fee, CERT_FEE)
            assert(not expect_failure)
            mark_logs("Sent certificate {}".format(cert), self.nodes, DEBUG_MODE)
            return cert 
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(expect_failure)
            if failure_reason is not None:
                assert(failure_reason in errorString)
            return None


    def run_test(self):
        '''
        The test creates a sc, send funds to it and then sends a certificates to it.
        It then sends more certificates with either the same or lower epochs, and checks that they all fail.
        Also, it sends another certificate with a higher but not consecutive epoch, and fails.
        Finally, it sends certificate with a valid epoch.
        '''

        # forward transfer amounts
        creation_amount = Decimal("50")
        bwt_amount_1 = Decimal("1")
        bwt_amount_2 = Decimal("2")
        bwt_amount_3 = Decimal("3")
        bwt_amount_4 = Decimal("4")

        addr_node2 = self.nodes[2].getnewaddress()

        mark_logs("Node 0 generates {} block".format(ForkHeights['NON_CEASING_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['NON_CEASING_SC'])
        self.sync_all()

        # generate wCertVk and constant
        self.mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = self.mcTest.generate_params(PARAMS_NAME, keyrot=True)
        self.constant = generate_random_field_element_hex()

        # generate sidechain
        cmdInput = {
            "version": 2,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": self.constant,
        }

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs("Node 0 created the SC {} spending {} coins via tx {}.".format(scid, creation_amount, creating_tx), self.nodes, DEBUG_MODE)

        sc_cr_block = self.nodes[0].generate(1)[0]
        self.sync_all()
        sc_creating_height = self.nodes[0].getblockcount()
        mark_logs("Node0 confirms sc creation generating 2 blocks", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(3)

        #------------------------------------------------
        mark_logs("## Test nok, wrong quality ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 0
        quality = -9
        ref_height = self.nodes[0].getblockcount()-2
        amount_cert_1 = {"address": addr_node2, "amount": bwt_amount_1}
        self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_1, True, "Invalid quality parameter")

        #------------------------------------------------
        mark_logs("## Test ok, epoch 0 ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 0
        ref_quality = 2
        ref_height = self.nodes[0].getblockcount()-2
        amount_cert_1 = {"address": addr_node2, "amount": bwt_amount_1}
        cert_1 = self.try_send_certificate(0, scid, epoch_number, ref_quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_1, False)

        #------------------------------------------------
        mark_logs("## Test nok, trying to overwrite ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        ref_height = self.nodes[0].getblockcount()-2
        amount_cert_1 = {"address": addr_node2, "amount": bwt_amount_1}
<<<<<<< HEAD
        self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_1, True, "invalid timing for certificate")
=======
        self.try_send_certificate(0, scid, epoch_number, ref_quality, ref_height, MBTR_SC_FEE_HIGH, FT_SC_FEE, amount_cert_1, True, "invalid timing for certificate")

        #------------------------------------------------
        mark_logs("## Test nok, trying to overwrite with a cert with less quality ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 0
        less_quality = 0
        ref_height = self.nodes[0].getblockcount()-2
        amount_cert_1 = {"address": addr_node2, "amount": bwt_amount_1}
        self.try_send_certificate(0, scid, epoch_number, less_quality, ref_height, MBTR_SC_FEE_HIGH, FT_SC_FEE, amount_cert_1, True, "invalid timing for certificate")
>>>>>>> 4b87d4fcb (more test updates)

        #------------------------------------------------
        mark_logs("## Test nok, trying to overwrite with a cert with higher quality ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 0
        more_quality = 100
        ref_height = self.nodes[0].getblockcount()-2
        amount_cert_1 = {"address": addr_node2, "amount": bwt_amount_1}
        self.try_send_certificate(0, scid, epoch_number, more_quality, ref_height, MBTR_SC_FEE_HIGH, FT_SC_FEE, amount_cert_1, True, "invalid timing for certificate")

        #------------------------------------------------
        mark_logs("## Generating blocks and checking info ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        assert_equal(True, cert_1 in self.nodes[0].getrawmempool())
        inv_bl_height = self.nodes[0].getblockcount()
        inv_bl_hash = self.nodes[0].generate(3)[0]
        self.sync_all()

        scinfo = self.nodes[2].getscinfo(scid, False, False)
        assert_equal(bwt_amount_1, creation_amount - scinfo['items'][0]['balance'])
        assert_equal(bwt_amount_1, scinfo['items'][0]['lastCertificateAmount'])
        assert_equal(cert_1, scinfo['items'][0]['lastCertificateHash'])
        assert_equal(ref_quality, scinfo['items'][0]['lastCertificateQuality'])

        winfo = self.nodes[2].getwalletinfo()
        assert_equal(bwt_amount_1, winfo['balance'])
        assert_equal(Decimal("0"), winfo['immature_balance'])
        assert_equal(1, winfo['txcount'])

        #------------------------------------------------
        mark_logs("## Test nok, epoch 2, non consecutive epochs ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 2
        quality = 0
        ref_height = self.nodes[0].getblockcount()-4
        amount_cert_2 = {"address": addr_node2, "amount": bwt_amount_2}
        self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_2, True, "invalid timing for certificate")

        self.sync_all()

        #------------------------------------------------
        mark_logs("## Test ok, epoch 1 ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 1
        quality = 5
        ref_height = self.nodes[0].getblockcount()-4
        amount_cert_2 = {"address": addr_node2, "amount": bwt_amount_2}
        cert_2 = self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE + 1, amount_cert_2, False)

        #------------------------------------------------
        mark_logs("## Test ok, epoch 2 (same mempool) ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 2
        quality = 0
        ref_height = self.nodes[0].getblockcount()-1
        amount_cert_3 = {"address": addr_node2, "amount": bwt_amount_3}
        cert_3 = self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE + 2, amount_cert_3, False)

        #------------------------------------------------
        mark_logs("## Test ok, send ft with amount lower than FT_SC_FEE in mempool, but ok with blockchain ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        fwt_amount = Decimal(0.5) # this amount is lower than mempool's FT_SC_FEE, but higher than blockchain's one
        fwd_tx = self.nodes[0].sc_send([{'toaddress': "abcd", 'amount': fwt_amount, "scid": scid, "mcReturnAddress": self.nodes[0].getnewaddress()}])
        assert(fwd_tx in self.nodes[0].getrawmempool())

        # FIXME(dr): this causes a stall because of the known issue with async batched proof verification for V2.
        # To help replicate, use '-scproofqueuesize=10' and '-scproofverificationdelay=100000' 
        #mark_logs("## Waiting for proof verification ##", self.nodes, DEBUG_MODE)
        #self.sync_all()
        #------------------------------------------------
        mark_logs("## Test nok, epoch 3 with old reference height (mempool) ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        for ref_height in range(self.nodes[0].getblockcount()-3, self.nodes[0].getblockcount()-1):
            epoch_number = 3
            quality = 0
            amount_cert_4 = {"address": addr_node2, "amount": bwt_amount_4}
            self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_4, True, "invalid timing for certificate")

        #------------------------------------------------
        mark_logs("## Generating 1 block ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        hash = self.nodes[0].generate(1)

        self.sync_all()

        #------------------------------------------------
        mark_logs("## Check info ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        scinfo = self.nodes[1].getscinfo(scid, False, False)
        assert_equal(bwt_amount_1+bwt_amount_2+bwt_amount_3-fwt_amount, creation_amount - (scinfo['items'][0]['balance']))
        assert_equal(bwt_amount_3, scinfo['items'][0]['lastCertificateAmount'])
        assert_equal(cert_3, scinfo['items'][0]['lastCertificateHash'])
        assert_equal(quality, scinfo['items'][0]['lastCertificateQuality'])

        #------------------------------------------------
        mark_logs("## Invalidate last block and check reverted info ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        self.nodes[0].invalidateblock(hash[0])

        scinfo = self.nodes[0].getscinfo(scid, False, False)
        assert_equal(bwt_amount_1, creation_amount - scinfo['items'][0]['balance'])
        assert_equal(bwt_amount_1, scinfo['items'][0]['lastCertificateAmount'])
        assert_equal(cert_1, scinfo['items'][0]['lastCertificateHash'])
        assert_equal(ref_quality, scinfo['items'][0]['lastCertificateQuality'])

        txmem = self.nodes[0].getrawmempool()
        assert_equal(len(txmem), 3)

        #------------------------------------------------
        mark_logs("## Reconsider last block ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        self.nodes[0].reconsiderblock(hash[0])

        #------------------------------------------------
        mark_logs("## Test nok, epoch 3 with old reference height (blockchain) ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        for ref_height in range(self.nodes[0].getblockcount()-3, self.nodes[0].getblockcount()-1):
            epoch_number = 3
            quality = 0
            amount_cert_4 = {"address": addr_node2, "amount": bwt_amount_4}
            self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_4, True, "invalid timing for certificate")

        self.sync_all()

        #------------------------------------------------
        mark_logs("## Stopping and restarting nodes ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)
        self.sync_all()

        #------------------------------------------------
        mark_logs("## Test nok, epoch 3 (after restart) ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 3
        quality = 0
        ref_height = self.nodes[0].getblockcount()-2
        amount_cert_4 = {"address": addr_node2, "amount": bwt_amount_4}
        self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_4, True, "invalid timing for certificate")

        #------------------------------------------------
        mark_logs("## Test invalidate and reconsider multiple blocks (h {})##".format(inv_bl_height), self.nodes, DEBUG_MODE)
        #------------------------------------------------
        self.nodes[0].invalidateblock(inv_bl_hash)
        cpool = self.nodes[0].getrawmempool()
        assert_equal(3, len(cpool)) # the last certificate has no reference anymore, so it's gone
        assert(cert_1 in cpool)
        assert(cert_2 in cpool)
        self.nodes[0].reconsiderblock(inv_bl_hash)
        assert_equal(0, len(self.nodes[0].getrawmempool())) # all the certificates and fwt are in the blockchain again

if __name__ == '__main__':
    ncsc_cert_epochs().main()

