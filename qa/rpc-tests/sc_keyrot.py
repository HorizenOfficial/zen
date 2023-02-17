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
NUMB_OF_NODES = 1
NC_EPOCH_LEN = 0
EPOCH_LEN = 10
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
MBTR_SC_FEE_HIGH = Decimal('0.5')
CERT_FEE = Decimal('0.00015')
PARAMS_NAME = "sc"
PARAMS_NAME_KEYROT = "sc_keyrot"


class ncsc_cert_epochs(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib', '-scproofqueuesize=0', '-logtimemicros=1']] * NUMB_OF_NODES)

    def try_send_certificate(self, params_name, nonceasing, scid, epoch_number, quality, ref_height, mbtr_fee, ft_fee, bt, prev_cert_hash, expect_failure, failure_reason=None):
        scid_swapped = str(swap_bytes(scid))
        ep, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], NC_EPOCH_LEN if nonceasing else EPOCH_LEN, True, ref_height)
        proof = self.mcTest.create_test_proof(params_name,
                                              scid_swapped,
                                              epoch_number,
                                              quality,
                                              mbtr_fee,
                                              ft_fee,
                                              epoch_cum_tree_hash,
                                              prev_cert_hash = prev_cert_hash,
                                              constant       = self.constant,
                                              pks            = [bt["address"]],
                                              amounts        = [bt["amount"]])

        mark_logs("Node {} sends cert of quality {} epoch {} ref {} with bwt of {}, expecting {}".format(0, quality, epoch_number, ref_height, bt["amount"], "failure" if expect_failure else "success"), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
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

    def create_sidechain(self, pname, version, epoch_len, rot):
        # generate sidechain v0 w/ keyrotation
        vk = self.mcTest.generate_params(pname, keyrot=rot)
        cmdInput = {
            "version": version,
            "withdrawalEpochLength": epoch_len,
            "toaddress": "dada",
            "amount": Decimal("1"),
            "wCertVk": vk,
            "constant": self.constant,
        }
        return self.nodes[0].sc_create(cmdInput)


    def run_test(self):
        '''
        This test checks that the expected configuration in terms of key rotation is enforced in combination with
        specific sidechain versions.

        mc-cryptolib does not have any notion of sidechain version, so the generation of parameters is always considered
        correct, even when we plan to use such parameters in deliberately wrong scenarios.

        Concretely, this test checks that submitting the first certificate with the wrong configuration for the given sidechain
        fails when it should. Subsequent certificates for v2 sidechains (i.e. w/ key rot) are tested in other tests
        (e.g. sc_cert_nonceasing).

        CHECKLIST:
        v0 with key rotation                   -> expect failure, since v0 does not support key rotation
        v0 without key rotation                -> expect success
        v2 without key rotation                -> expect failure, since key rotation is enforced for v2 and above
        v2 with key rotation, wrong first hash -> expect failure, since the first hash must be phantom_hash by definition
        v2 with key rotation, phantom_hash     -> expect success
        '''

        #------------------------------------------------
        mark_logs("## Test setup and SCs creation ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------

        mark_logs("Node 0 generates {} block".format(ForkHeights['NON_CEASING_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['NON_CEASING_SC'])

        # generate wCertVk and constant
        self.mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        self.constant = generate_random_field_element_hex()

        ### V0 ###
        sc0_keyrot_id = self.create_sidechain(PARAMS_NAME_KEYROT + "0", 0, EPOCH_LEN, True)['scid']
        sc0_id        = self.create_sidechain(PARAMS_NAME + "0", 0, EPOCH_LEN, False)['scid']

        mark_logs("Node 0 created the SCv0 w/ keyrot {}.".format(sc0_keyrot_id), self.nodes, DEBUG_MODE)
        mark_logs("Node 0 created the SCv0 w/o keyrot {}.".format(sc0_id), self.nodes, DEBUG_MODE)


        ### V2 ###
        sc2_keyrot_id = self.create_sidechain(PARAMS_NAME_KEYROT + "2", 2, NC_EPOCH_LEN, True)['scid']
        sc2_id        = self.create_sidechain(PARAMS_NAME + "2", 2, NC_EPOCH_LEN, False)['scid']

        mark_logs("Node 0 created the nonceasing SCv2 w/ keyrot {}.".format(sc2_keyrot_id), self.nodes, DEBUG_MODE)
        mark_logs("Node 0 created the nonceasing SCv2 w/o keyrot {}.".format(sc2_id), self.nodes, DEBUG_MODE)

        mark_logs("Node0 confirms sc creation generating 10 blocks", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(10)


        #------------------------------------------------
        mark_logs("## Test SC v0 with keyrot, expect failure ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 0
        quality = 10

        addr_node0 = self.nodes[0].getnewaddress()
        bwt_amount_1 = Decimal("1")
        amount_cert_1 = {"address": addr_node0, "amount": bwt_amount_1}

        self.try_send_certificate(PARAMS_NAME_KEYROT + "0",
                                  False,
                                  sc0_keyrot_id,
                                  epoch_number,
                                  quality,
                                  self.nodes[0].getblockcount(),
                                  MBTR_SC_FEE,
                                  FT_SC_FEE,
                                  amount_cert_1,
                                  0, # valid prev_cert_hash
                                  True,
                                  "bad-sc-cert-proof"
                                 )

        #------------------------------------------------
        mark_logs("## Test SC v0 w/o keyrot, expect success ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        self.try_send_certificate(PARAMS_NAME + "0",
                                  False,
                                  sc0_id,
                                  epoch_number,
                                  quality,
                                  self.nodes[0].getblockcount(),
                                  MBTR_SC_FEE,
                                  FT_SC_FEE,
                                  amount_cert_1,
                                  None, # no prev_cert_hash
                                  False
                                 )

        #------------------------------------------------
        mark_logs("## Test SC v2 (nonceasing) w/o keyrot, expect failure ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 0
        quality = 0

        addr_node0 = self.nodes[0].getnewaddress()
        bwt_amount_1 = Decimal("1")
        amount_cert_1 = {"address": addr_node0, "amount": bwt_amount_1}

        self.try_send_certificate(PARAMS_NAME + "2",
                                  False,
                                  sc2_id,
                                  epoch_number,
                                  quality,
                                  self.nodes[0].getblockcount(),
                                  MBTR_SC_FEE,
                                  FT_SC_FEE,
                                  amount_cert_1,
                                  None,
                                  True,
                                  "bad-sc-cert-proof"
                                 )

        #------------------------------------------------
        mark_logs("## Test SC v2 (nonceasing) with keyrot with random key, expect failure ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        self.try_send_certificate(PARAMS_NAME_KEYROT + "2",
                                  False,
                                  sc2_keyrot_id,
                                  epoch_number,
                                  quality,
                                  self.nodes[0].getblockcount(),
                                  MBTR_SC_FEE,
                                  FT_SC_FEE,
                                  amount_cert_1,
                                  generate_random_field_element_hex(),
                                  True,
                                  "bad-sc-cert-proof"
                                 )

        #------------------------------------------------
        mark_logs("## Test SC v2 (nonceasing) with keyrot, expect success ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        self.try_send_certificate(PARAMS_NAME_KEYROT + "2",
                                  False,
                                  sc2_keyrot_id,
                                  epoch_number,
                                  quality,
                                  self.nodes[0].getblockcount(),
                                  MBTR_SC_FEE,
                                  FT_SC_FEE,
                                  amount_cert_1,
                                  0, # 0 means that PHANTOM_HASH will be used
                                  False
                                 )


if __name__ == '__main__':
    ncsc_cert_epochs().main()

