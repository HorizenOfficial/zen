#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, disconnect_nodes, connect_nodes_bi, mark_logs,\
    get_epoch_data, wait_bitcoinds, stop_nodes, \
    swap_bytes
from test_framework.mc_test.mc_test import *
import os
import time
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH = 0 # this test is for non ceasing sc, hence EPOCH_LENGTH must be 0!

FT_SC_FEE      = Decimal('0')
HIGH_FT_SC_FEE = Decimal('1')
MBTR_SC_FEE    = Decimal('0.5')
CERT_FEE       = Decimal('0.00015')
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
        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.sync_all()

    def split_network(self):
        # Split the network of three nodes into nodes 0 and 1
        assert not self.is_network_split
        disconnect_nodes(self.nodes[0], 1)
        disconnect_nodes(self.nodes[1], 0)
        self.is_network_split = True

    def join_network(self):
        # Join the (previously split) network pieces together: 0-1
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 0, 1)
        time.sleep(2)
        self.is_network_split = False


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
        It then sends more certificates expected to be rejected, to check that all the non ceasing sc rules are enforced.
        Finally, it checks that the behavior is correct also in case of disconnections of one or more blocks.

        CHECKLIST:

        - Basic Rules:
            - cannot send negative cert quality
            - cannot send first certificate with reference older than sidechain creation
            - can    send first certificate with any positive quality and reference equal or newer than sidechain creation
            - cannot overwrite a certificate in the mempool with any another (regardless of quality and reference)
            - cannot have more than 1 certificate in mempool for a given sidechain (regardless of epoch)
            - cannot send Cert N if Cert N-1 has not been received yet
            - cannot send Cert N referencing a block prior to the inclusion of Cert N-1
              (which implies cannot have Cert N referencing a block prior or equal to the reference of Cert N-1)
            - can    send Cert N referencing a block higher than the inclusion of Cert N-1

        - Dynamic interactions:
            - last referenced and inclusion heights are still considered after nodes restart (tests serialization)
            - can send tx with a fee not ok with the mempool one, but ok with the blockchain one
            - on block disconnection certificates which remain valid are added to the mempool
            - on block disconnection if a reference for a certificate is removed, such certificate is removed from the mempool
            - on block connection, if a certificate for epoch N is included in the block for a given sc, any certificate in the mempool for such sidechain is removed
            - disconnecting multiple blocks at once yields the expected result (oldest still valid certificate in mempool, others removed)
        '''

        # forward transfer amounts
        creation_amount = Decimal("50")
        bwt_amount_1    = Decimal("1")
        bwt_amount_2    = Decimal("2")
        bwt_amount_3_0  = Decimal("3")
        bwt_amount_3_1  = Decimal("4")

        addr_node1 = self.nodes[1].getnewaddress()

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
        mark_logs("Node 0 created the SC {} spending {} coins via tx {}.".format(scid, creation_amount, creating_tx), self.nodes, DEBUG_MODE)

        mark_logs("Node0 confirms sc creation generating 2 blocks", self.nodes, DEBUG_MODE)
        sc_cr_block = self.nodes[0].generate(2)[0]
        sc_creation_height = self.nodes[0].getblockcount() - 1
        self.sync_all()

        #------------------------------------------------
        mark_logs("## Test nok, wrong quality ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 0
        quality = -9
        ref_height = sc_creation_height
        amount_cert_1 = {"address": addr_node1, "amount": bwt_amount_1}
        self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_1, True, "Invalid quality parameter")

        #------------------------------------------------
        mark_logs("## Test nok, reference older than sc creation ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 0
        ref_quality = 2
        ref_height = sc_creation_height - 1
        amount_cert_1 = {"address": addr_node1, "amount": bwt_amount_1}
        self.try_send_certificate(0, scid, epoch_number, ref_quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_1, True, "invalid timing for certificate")

        #------------------------------------------------
        mark_logs("## Test ok, epoch 0 ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 0
        ref_quality = 2
        ref_height = sc_creation_height
        amount_cert_1 = {"address": addr_node1, "amount": bwt_amount_1}
        cert_1 = self.try_send_certificate(0, scid, epoch_number, ref_quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_1, False)

        #------------------------------------------------
        mark_logs("## Test nok, trying to overwrite with a cert with same quality ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        ref_height = sc_creation_height
        amount_cert_1 = {"address": addr_node1, "amount": bwt_amount_1}
        self.try_send_certificate(0, scid, epoch_number, ref_quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_1, True, "invalid timing for certificate")

        #------------------------------------------------
        mark_logs("## Test nok, trying to overwrite with a cert with less quality ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 0
        less_quality = 0
        ref_height = sc_creation_height
        amount_cert_1 = {"address": addr_node1, "amount": bwt_amount_1}
        self.try_send_certificate(0, scid, epoch_number, less_quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_1, True, "invalid timing for certificate")

        #------------------------------------------------
        mark_logs("## Test nok, trying to overwrite with a cert with higher quality ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 0
        more_quality = 100
        ref_height = sc_creation_height
        amount_cert_1 = {"address": addr_node1, "amount": bwt_amount_1}
        self.try_send_certificate(0, scid, epoch_number, more_quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_1, True, "invalid timing for certificate")

        #------------------------------------------------
        mark_logs("## Test nok, trying to overwrite with more recent reference ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        ref_height = sc_creation_height + 1
        amount_cert_1 = {"address": addr_node1, "amount": bwt_amount_1}
        self.try_send_certificate(0, scid, epoch_number, ref_quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_1, True, "invalid timing for certificate")

        #------------------------------------------------
        mark_logs("## Test nok, epoch 1 (epoch 0 is still in mempool) ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 1
        quality = 5
        ref_height = sc_creation_height + 1
        amount_cert_2 = {"address": addr_node1, "amount": bwt_amount_2}
        cert_2 = self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE + 1, amount_cert_2, True, "bad-sc-cert-conflict")


        #------------------------------------------------
        mark_logs("## Generating blocks and checking info ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        assert_equal(True, cert_1 in self.nodes[0].getrawmempool())
        c0_height = self.nodes[0].getblockcount() + 1
        c0_block = self.nodes[0].generate(2)[0]
        self.sync_all()

        scinfo = self.nodes[1].getscinfo(scid, False, False)
        assert_equal(bwt_amount_1, creation_amount - scinfo['items'][0]['balance'])
        assert_equal(bwt_amount_1, scinfo['items'][0]['lastCertificateAmount'])
        assert_equal(cert_1, scinfo['items'][0]['lastCertificateHash'])
        assert_equal(ref_quality, scinfo['items'][0]['lastCertificateQuality'])

        winfo = self.nodes[1].getwalletinfo()
        assert_equal(bwt_amount_1, winfo['balance'])
        assert_equal(Decimal("0"), winfo['immature_balance'])
        assert_equal(1, winfo['txcount'])

        #------------------------------------------------
        mark_logs("## Test nok, epoch 2, non consecutive epochs ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 2
        quality = 0
        ref_height = self.nodes[0].getblockcount()-4
        amount_cert_2 = {"address": addr_node1, "amount": bwt_amount_2}
        self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_2, True, "invalid timing for certificate")

        #------------------------------------------------
        mark_logs("## Test nok, epoch 1 w/ ref. height lower than Cert0's block ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 1
        quality = 5
        ref_height = c0_height - 1
        amount_cert_2 = {"address": addr_node1, "amount": bwt_amount_2}
        cert_2 = self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE + 1, amount_cert_2, True, "invalid timing for certificate")

        #------------------------------------------------
        mark_logs("## Test ok, epoch 1 w/ ref. height greater than Cert0's block ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 1
        quality = 5
        ref_height = c0_height + 1
        amount_cert_2 = {"address": addr_node1, "amount": bwt_amount_2}
        cert_2 = self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, HIGH_FT_SC_FEE, amount_cert_2, False)

        self.sync_all()

        #------------------------------------------------
        mark_logs("## Test ok, send ft with amount lower than HIGH_FT_SC_FEE in mempool, but ok with FT_SC_FEE in blockchain ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        fwt_amount = Decimal(0.5)
        fwd_tx = self.nodes[0].sc_send([{'toaddress': "abcd", 'amount': fwt_amount, "scid": scid, "mcReturnAddress": self.nodes[0].getnewaddress()}])
        assert(fwd_tx in self.nodes[0].getrawmempool())

        #------------------------------------------------
        mark_logs("## Generating 1 block to include certificate of epoch 1 ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        hash_block_c2 = self.nodes[0].generate(1)[0]
        self.sync_all()

        #------------------------------------------------
        mark_logs("## Split network ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        self.split_network()

        #------------------------------------------------
        mark_logs("## Test ok, node 1 sends epoch 2 certificate ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 2
        quality = 0
        ref_height = self.nodes[1].getblockcount()
        amount_cert_3_1 = {"address": addr_node1, "amount": bwt_amount_3_1}
        cert_3_1 = self.try_send_certificate(1, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE + 2, amount_cert_3_1, False)

        #------------------------------------------------
        mark_logs("## Test ok, node 0 sends another epoch 2 certificate ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 2
        quality = 0
        ref_height = self.nodes[0].getblockcount()
        amount_cert_3_0 = {"address": addr_node1, "amount": bwt_amount_3_0}
        cert_3_0 = self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE + 2, amount_cert_3_0, False)

        #------------------------------------------------
        mark_logs("## Node0 generates 1 block to include its own certificate of epoch 2 ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        c3_block =  self.nodes[0].generate(1)[0]
        c3_height = self.nodes[0].getblockcount()

        #------------------------------------------------
        mark_logs("## Join network ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        self.join_network()
        self.sync_all()

        #------------------------------------------------
        mark_logs("## Check info ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        assert_equal(len(self.nodes[1].getrawmempool()), 0) # Node1 dropped cert_3_1
        scinfo = self.nodes[1].getscinfo(scid, False, False)
        assert_equal(bwt_amount_1 + bwt_amount_2 + bwt_amount_3_0 - fwt_amount, creation_amount - (scinfo['items'][0]['balance']))
        assert_equal(bwt_amount_3_0, scinfo['items'][0]['lastCertificateAmount'])
        assert_equal(cert_3_0, scinfo['items'][0]['lastCertificateHash'])
        assert_equal(quality, scinfo['items'][0]['lastCertificateQuality'])

        #------------------------------------------------
        mark_logs("## Invalidate last block and check reverted info ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        self.nodes[0].invalidateblock(c3_block)

        scinfo = self.nodes[0].getscinfo(scid, False, False)
        assert_equal(bwt_amount_1 + bwt_amount_2 - fwt_amount, creation_amount - scinfo['items'][0]['balance'])
        assert_equal(bwt_amount_2, scinfo['items'][0]['lastCertificateAmount'])
        assert_equal(cert_2, scinfo['items'][0]['lastCertificateHash'])
        assert_equal(5, scinfo['items'][0]['lastCertificateQuality'])

        cpool = self.nodes[0].getrawmempool()
        assert_equal(len(cpool), 1)
        assert(cert_3_0 in cpool)

        #------------------------------------------------
        mark_logs("## Invalidate last block and check reverted info ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        self.nodes[0].invalidateblock(hash_block_c2)

        scinfo = self.nodes[0].getscinfo(scid, False, False)
        assert_equal(bwt_amount_1, creation_amount - scinfo['items'][0]['balance'])
        assert_equal(bwt_amount_1, scinfo['items'][0]['lastCertificateAmount'])
        assert_equal(cert_1, scinfo['items'][0]['lastCertificateHash'])
        assert_equal(2, scinfo['items'][0]['lastCertificateQuality'])

        cpool = self.nodes[0].getrawmempool()
        if len(cpool) > 0:
            assert(cert_2 in cpool)
        if len(cpool) > 1:
            assert(fwd_tx in cpool)
        assert(len(cpool) <= 2)

        #------------------------------------------------
        mark_logs("## Reconsider last 2 blocks ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        self.nodes[0].reconsiderblock(hash_block_c2)

        scinfo = self.nodes[0].getscinfo(scid, False, False)
        assert_equal(bwt_amount_1 + bwt_amount_2 + bwt_amount_3_0 - fwt_amount, creation_amount - scinfo['items'][0]['balance'])
        assert_equal(bwt_amount_3_0, scinfo['items'][0]['lastCertificateAmount'])
        assert_equal(cert_3_0, scinfo['items'][0]['lastCertificateHash'])
        assert_equal(0, scinfo['items'][0]['lastCertificateQuality'])

        #------------------------------------------------
        mark_logs("## Stopping and restarting nodes ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)
        self.sync_all()

        #------------------------------------------------
        mark_logs("## Test nok, low reference height (after restart) ##", self.nodes, DEBUG_MODE)
        #------------------------------------------------
        epoch_number = 3
        quality = 0
        ref_height = c3_height - 1
        amount_cert_4 = {"address": addr_node1, "amount": bwt_amount_3_1}
        self.try_send_certificate(0, scid, epoch_number, quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_4, True, "invalid timing for certificate")

        #------------------------------------------------
        mark_logs("## Test invalidate and reconsider multiple blocks (h {})##".format(c0_height), self.nodes, DEBUG_MODE)
        #------------------------------------------------
        self.nodes[0].invalidateblock(c0_block)

        cpool = self.nodes[0].getrawmempool()
        # certificates 2 and 3 have no reference anymore, so they are gone
        # fwd_tx may or may not depend on Cert2, so it may or may not be still around
        if len(cpool) > 0:
            assert(cert_1 in cpool)
        if len(cpool) > 1:
            assert(fwd_tx in cpool)
        assert(len(cpool) <= 2)

        self.nodes[0].reconsiderblock(c0_block)
        assert_equal(0, len(self.nodes[0].getrawmempool())) # everything is in the blockchain again

if __name__ == '__main__':
    ncsc_cert_epochs().main()

