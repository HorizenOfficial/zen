#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException, AuthServiceProxy
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
    get_epoch_data, assert_false, assert_true, swap_bytes, start_node
from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 1
EPOCH_LENGTH = 17
#EPOCH_LENGTH = 5
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')


class sc_proof_verifier_low_priority_threads(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = [start_node(0, self.options.tmpdir,
                                extra_args=['-debug=py', '-debug=sc', '-debug=mempool',
                                    '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib',
                                    '-scproofqueuesize=0', '-logtimemicros=1'],
                                timewait=30)]  # 30 seconds of timeout

    def run_test(self):

        '''
        The test creates a sc, send funds to it and then:
        ( 1) Switch ON the CZendooLowPrioThreadGuard that should prevent any mempool Cert to be included,
            because of ProofVerifier threads are on pause.
        ( 2) Switch OFF the CZendooLowPrioThreadGuard -> certificate must be applied to mempool.
        ( 3) Switch ON the CZendooLowPrioThreadGuard and try to generate the block with Certificate - must be successful
        '''

        # forward transfer amounts
        creation_amount = Decimal("0.5")
        fwt_amount = Decimal("50")
        bwt_amount_bad = Decimal("100.0")
        bwt_amount = Decimal("50")

        self.nodes[0].getblockhash(0)

        mark_logs("Node generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])

        # SC creation
        # Generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()
        cmdInput = {
            'version': 0,
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
        mark_logs("Node created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)

        decoded_tx = self.nodes[0].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        mark_logs("Node confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], Decimal(0))
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts'][0]['amount'], creation_amount)

        # Fwd Transfer to Sc
        bal_before_fwd_tx = self.nodes[0].getbalance("", 0)
        mark_logs("Node balance before fwd tx: {}".format(bal_before_fwd_tx), self.nodes, DEBUG_MODE)
        mc_return_address = self.nodes[0].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount, "scid": scid, "mcReturnAddress": mc_return_address}]
        fwd_tx = self.nodes[0].sc_send(cmdInput)
        mark_logs("Node transfers {} coins to SC with tx {}...".format(fwt_amount, fwd_tx), self.nodes, DEBUG_MODE)

        mark_logs("Node confirms fwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)

        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], Decimal(0))
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts'][0]['amount'], creation_amount)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts'][1]['amount'], fwt_amount)

        nblocks = EPOCH_LENGTH - 2
        mark_logs("Node0 generating {} more blocks to achieve end of withdrawal epoch".format(nblocks), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(nblocks)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc balance has matured
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)

        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        addr_node0 = self.nodes[0].getnewaddress()

        #Create proof for WCert
        quality = 10
        proof = mcTest.create_test_proof("sc1",
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant,
                                         pks      = [addr_node0],
                                         amounts  = [bwt_amount])

        amount_cert_1 = [{"address": addr_node0, "amount": bwt_amount}]

        # Enable CZendooLowPrioThreadGuard
        mark_logs("Enable CZendooLowPrioThreadGuard...", self.nodes, DEBUG_MODE)
        res = self.nodes[0].setproofverifierlowpriorityguard(True)
        assert_equal(res["enabled"], True)

        # Try to send WCert - should fail because of the timeout: mempool proof verifier has low priority
        mark_logs("Node sends a certificate while CZendooLowPrioThreadGuard is enabled...", self.nodes, DEBUG_MODE)
        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except Exception as e:
            assert("timed out" == str(e))
            # Establish new AuthServiceProxy, because previous one is dead after the timeout error
            self.nodes[0] = AuthServiceProxy(self.nodes[0].get_service_url(), timeout=10)
            mark_logs("Send certificate failed with reason {}".format(e), self.nodes, DEBUG_MODE)

        # Disable CZendooLowPrioThreadGuard
        mark_logs("Disable CZendooLowPrioThreadGuard...", self.nodes, DEBUG_MODE)
        res = self.nodes[0].setproofverifierlowpriorityguard(False)
        assert_equal(res["enabled"], False)


        # Try to send WCert
        mark_logs("Node sends a certificate while CZendooLowPrioThreadGuard is enabled...", self.nodes, DEBUG_MODE)
        try:
            cert_epoch_0 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                                                          epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE,
                                                          MBTR_SC_FEE, CERT_FEE)
            assert (len(cert_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        # Enable CZendooLowPrioThreadGuard
        mark_logs("Enable CZendooLowPrioThreadGuard.", self.nodes, DEBUG_MODE)
        res = self.nodes[0].setproofverifierlowpriorityguard(True)
        assert_equal(res["enabled"], True)

        # Generate 1 block with Certificate, CZendooLowPrioThreadGuard should not effect.
        mark_logs("Node generates a block when CZendooLowPrioThreadGuard is enabled...", self.nodes, DEBUG_MODE)
        block_hash = self.nodes[0].generate(1)[0]
        assert_equal(0, self.nodes[0].getmempoolinfo()["size"], "Certificate expected to be removed from MC node mempool.")
        assert_equal(1, len(self.nodes[0].getblock(block_hash)["cert"]),
                     "MC block expected to contain 1 Certificate.")
        assert_equal(cert_epoch_0, self.nodes[0].getblock(block_hash)["cert"][0],
                     "MC block expected to contain certificate.")


if __name__ == '__main__':
    sc_proof_verifier_low_priority_threads().main()
