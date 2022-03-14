#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs,\
    get_epoch_data, assert_false, assert_true, swap_bytes
from test_framework.test_framework import MINIMAL_SC_HEIGHT, MINER_REWARD_POST_H200
from test_framework.mc_test.mc_test import CertTestUtils, generate_random_field_element_hex
import os
import pprint
import time
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 3
EPOCH_LENGTH = 17
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
        The test checks that the "GetBlockTemplate" command correctly detects a new certificate in the mempool,
        in the same way as it happens for normal transactions.
        '''

        # amounts
        creation_amount = Decimal("10")
        bwt_amount = Decimal("1.0")

        mark_logs("Node 0 generates {} block".format(MINIMAL_SC_HEIGHT), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(MINIMAL_SC_HEIGHT)
        self.sync_all()

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": constant,
        }

        ret = self.nodes[0].sc_create(cmdInput)
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs("Node 0 created a SC", self.nodes, DEBUG_MODE)

        nblocks = EPOCH_LENGTH
        mark_logs("Node 0 generating {} more blocks to confirm the sidechain and reach the end of withdrawal epoch".format(nblocks), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(nblocks)
        self.sync_all()

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        addr_node1 = self.nodes[1].getnewaddress()

        #Create proof for WCert
        quality = 10
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node1], [bwt_amount])

        amount_cert_1 = [{"address": addr_node1, "amount": bwt_amount}]

        cur_h = self.nodes[0].getblockcount()
        ret = self.nodes[0].getscinfo(scid, True, False)['items'][0]
        ceas_h = ret['ceasingHeight']
        ceas_limit_delta = ceas_h - cur_h - 1

        mark_logs("Node0 generating {} blocks reaching the third to last block before the SC ceasing".format(ceas_limit_delta), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ceas_limit_delta - 2)
        self.sync_all()

        mark_logs("\nCall GetBlockTemplate on each node to create a cached (empty) version", self.nodes, DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            self.nodes[i].getblocktemplate()

        mark_logs("Node 0 sends a normal mainchain transaction to mempool and checks that it's not immediately included into the block template", self.nodes, DEBUG_MODE)
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 0.1)
        self.sync_all()

        for i in range(0, NUMB_OF_NODES):
            assert(len(self.nodes[i].getblocktemplate()['certificates']) == 0)
            assert(len(self.nodes[i].getblocktemplate()['transactions']) == 0)

        GET_BLOCK_TEMPLATE_DELAY = 5 # Seconds
        mark_logs("Wait {} seconds and check that the transaction is now included into the block template".format(GET_BLOCK_TEMPLATE_DELAY), self.nodes, DEBUG_MODE)
        time.sleep(GET_BLOCK_TEMPLATE_DELAY)
        for i in range(0, NUMB_OF_NODES):
            assert(len(self.nodes[i].getblocktemplate()['certificates']) == 0)
            assert(len(self.nodes[i].getblocktemplate()['transactions']) == 1)

        mark_logs("Node 0 mines one block to clean the mempool", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("\nCall GetBlockTemplate on each node to create a new cached version", self.nodes, DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            self.nodes[i].getblocktemplate()

        mark_logs("Node 0 sends a certificate", self.nodes, DEBUG_MODE)
        try:
            cert_epoch_0 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_epoch_0) > 0)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Check that the certificate is not immediately included into the block template", self.nodes, DEBUG_MODE)

        for i in range(0, NUMB_OF_NODES):
            assert(len(self.nodes[i].getblocktemplate()['certificates']) == 0)
            assert(len(self.nodes[i].getblocktemplate()['transactions']) == 0)

        GET_BLOCK_TEMPLATE_DELAY = 5 # Seconds
        mark_logs("Wait {} seconds and check that the certificate is now included into the block template".format(GET_BLOCK_TEMPLATE_DELAY), self.nodes, DEBUG_MODE)
        time.sleep(GET_BLOCK_TEMPLATE_DELAY)
        for i in range(0, NUMB_OF_NODES):
            assert(len(self.nodes[i].getblocktemplate()['certificates']) == 1)
            assert(len(self.nodes[i].getblocktemplate()['transactions']) == 0)

        mark_logs("Node 0 mines one block to clean the mempool", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # Generate proof before the call of `getblocktemplate`.
        # It is a time consuming operation, so may take more than GET_BLOCK_TEMPLATE_DELAY seconds.
        quality = quality + 1
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant,
            [addr_node1], [bwt_amount])

        mark_logs("\nCall GetBlockTemplate on each node to create a new cached version", self.nodes, DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            self.nodes[i].getblocktemplate()

        mark_logs("Node 0 sends a normal transaction and a certificate", self.nodes, DEBUG_MODE)
        
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 0.1)

        try:
            cert_epoch_0 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_epoch_0) > 0)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Check that the certificate is not immediately included into the block template", self.nodes, DEBUG_MODE)

        for i in range(0, NUMB_OF_NODES):
            assert(len(self.nodes[i].getblocktemplate()['certificates']) == 0)
            assert(len(self.nodes[i].getblocktemplate()['transactions']) == 0)

        GET_BLOCK_TEMPLATE_DELAY = 5 # Seconds
        mark_logs("Wait {} seconds and check that the certificate is now included into the block template".format(GET_BLOCK_TEMPLATE_DELAY), self.nodes, DEBUG_MODE)
        time.sleep(GET_BLOCK_TEMPLATE_DELAY)
        for i in range(0, NUMB_OF_NODES):
            assert(len(self.nodes[i].getblocktemplate()['certificates']) == 1)
            assert(len(self.nodes[i].getblocktemplate()['transactions']) == 1)

        for i in range(0, NUMB_OF_NODES):
            # Check that `getblocktemplate` doesn't include "merkleTree" and "scTxsCommitment" if not explicitly requested
            gbt = self.nodes[i].getblocktemplate()
            assert_false('merkleTree' in gbt)
            assert_false('scTxsCommitment' in gbt)
            gbt = self.nodes[i].getblocktemplate({}, False)
            assert_false('merkleTree' in gbt)
            assert_false('scTxsCommitment' in gbt)

            # Check that `getblocktemplate` "merkleTree" and "scTxsCommitment" match `getblockmerkleroots`
            gbt = self.nodes[i].getblocktemplate({}, True)
            roots = self.nodes[i].getblockmerkleroots([gbt['coinbasetxn']['data']] + [x['data'] for x in gbt['transactions']], [x['data'] for x in gbt['certificates']])
            assert_equal(gbt['merkleTree'], roots['merkleTree'])
            assert_equal(gbt['scTxsCommitment'], roots['scTxsCommitment'])

if __name__ == '__main__':
    sc_cert_base().main()
