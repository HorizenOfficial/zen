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
import time

DEBUG_MODE = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH = 20
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')

class quality_nodes(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_nodes(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-scproofqueuesize=0', '-logtimemicros=1']] * NUMB_OF_NODES)

    def run_test(self):

        '''
        The test creates a sc, send funds to it and then sends a certificates to it,
        verifying certificates moving to mempool after rejoining network
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
        constant = generate_random_field_element_hex()
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk_1,
            "constant": constant,
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
        #assert_equal(bal_before_sc_creation, bal_after_sc_creation + creation_amount + creation_amount - fee_sc_creation - fee_sc_creation)

        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], Decimal(0))
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts'][0]['amount'], creation_amount)

        # Fwd Transfer to SC 1
        bal_before_fwd_tx = self.nodes[0].getbalance("", 0)
        mc_return_address = self.nodes[0].getnewaddress()
        mark_logs("Node0 balance before fwd tx: {}".format(bal_before_fwd_tx), self.nodes, DEBUG_MODE)
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount, "scid": scid, 'mcReturnAddress': mc_return_address}]
        fwd_tx = self.nodes[0].sc_send(cmdInput)
        mark_logs("Node0 transfers {} coins to SC 1 with tx {}...".format(fwt_amount, fwd_tx), self.nodes, DEBUG_MODE)
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


        bal_after_fwd_tx = self.nodes[0].getbalance("", 0)
        mark_logs("Node0 balance after fwd: {}".format(bal_after_fwd_tx), self.nodes, DEBUG_MODE)

        mark_logs("Node0 generating more blocks to achieve end of withdrawal epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(EPOCH_LENGTH - 2)
        self.sync_all()
        h = self.nodes[0].getblockcount()

        mark_logs("Chain height={}".format(h), self.nodes, DEBUG_MODE)

        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc balance has matured
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)

        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        addr_node0 = self.nodes[0].getnewaddress()
        self.sync_all()

        amount_cert_0 = [{"address": addr_node0, "amount": bwt_amount}]

        self.split_network(0)

        #----------------------Network Part 1

        # Create Cert1 with quality 100 and place it in node0
        mark_logs("Height: {}. cration height {}".format(self.nodes[0].getblockcount(), sc_creating_height), self.nodes, DEBUG_MODE)
        mark_logs("Height: {}. Epoch {}".format(self.nodes[0].getblockcount(), epoch_number), self.nodes, DEBUG_MODE)
        mark_logs("Create Cert1 with quality 100 and place it in node0", self.nodes, DEBUG_MODE)
        quality = 100
        proof = mcTest.create_test_proof(vk_tag_1,
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant,
                                         pks      = [addr_node0],
                                         amounts  = [bwt_amount])
        try:
            cert_1_epoch_0 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_0, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_1_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_1_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_1_epoch_0 in self.nodes[0].getrawmempool())
        cert1_mined = self.nodes[0].generate(2)[0]
        assert_true(cert_1_epoch_0 in self.nodes[0].getblock(cert1_mined, True)['cert'])

        #----------------------Network Part 2

        # Create Cert2 with quality 100 and place it in node1
        mark_logs("Create Cert2 with quality 100 and place it in node1", self.nodes, DEBUG_MODE)
        addr_node1 = self.nodes[1].getnewaddress()
        amount_cert_1 = [{"address": addr_node1, "amount": bwt_amount}]
        quality = 100
        proof = mcTest.create_test_proof(vk_tag_1,
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant,
                                         pks      = [addr_node1],
                                         amounts  = [bwt_amount])
        try:
            cert_2_epoch_0 = self.nodes[1].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_2_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_2_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        #Cert should be in mempool and could be placed in node_1 block
        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_2_epoch_0 in self.nodes[1].getrawmempool())
        cert2_mined = self.nodes[1].generate(1)[0]
        assert_true(cert_2_epoch_0 in self.nodes[1].getblock(cert2_mined, True)['cert'])

        self.join_network(0)

        self.sync_all()
        assert_true(cert_1_epoch_0 in self.nodes[0].getblock(cert1_mined, True)['cert'])

        try:
            certs = self.nodes[0].getblock(cert2_mined, True)['cert']
            assert (False)
        except JSONRPCException as e:
            mark_logs("Cert2 is not in blockchain", self.nodes, DEBUG_MODE)

        # it is not even in any mempool since cert1 with quality 100 is already in block chain
        assert_false(cert_2_epoch_0 in self.nodes[0].getrawmempool())
        assert_false(cert_2_epoch_0 in self.nodes[1].getrawmempool())

        self.nodes[0].generate(EPOCH_LENGTH - 2)
        self.sync_all()

        self.split_network(0)

        #----------------------Network Part 1

        # Create Cert3 with quality 100 and place it in node0
        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("Height: {}. creation height {}".format(self.nodes[0].getblockcount(), sc_creating_height), self.nodes, DEBUG_MODE)
        mark_logs("Height: {}. Epoch {}".format(self.nodes[0].getblockcount(), epoch_number), self.nodes, DEBUG_MODE)
        mark_logs("Create Cert1 with quality 100 and place it in node0", self.nodes, DEBUG_MODE)
        quality = 100
        amount_cert_0 = [{"address": addr_node0, "amount": bwt_amount}]
        proof = mcTest.create_test_proof(vk_tag_1,
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant,
                                         pks      = [addr_node0],
                                         amounts  = [bwt_amount])
        try:
            cert_1_epoch_1 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_0, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_1_epoch_1) > 0)
            mark_logs("Certificate is {}".format(cert_1_epoch_1), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_1_epoch_1 in self.nodes[0].getrawmempool())

        # generate 3 blocks thus reaching the end of the submission window
        #   
        #  <- - -     epoch N = 20          - - ->|<- - -     epoch N+1 = 20        - - ->
        # |                                       |                                       |
        # | sub wlen = 4  +                       | sub wlen = 4  +                       |
        # |               !                       |               !                       |
        # |               !                       |         X     !                       |
        # |---|---|---|---|---  ...   ... ...  ---|---|---|---|---|---  ...   ... ...  ---|   
        #
        #  242         245                     261         264
        #                                                   ^
        #                                                   |
        #  - - - - - - - - - - - - - - - - - - - - - - - - -+   
        cert1_mined = self.nodes[0].generate(3)[0]
        assert_true(cert_1_epoch_1 in self.nodes[0].getblock(cert1_mined, True)['cert'])

        #----------------------Network Part 2

        # Create Cert2 with quality 100 and place it in node1
        mark_logs("Create Cert2 with quality 100 and place it in node1", self.nodes, DEBUG_MODE)
        amount_cert_1 = [{"address": addr_node1, "amount": bwt_amount}]
        quality = 110
        proof = mcTest.create_test_proof(vk_tag_1,
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant,
                                         pks      = [addr_node1],
                                         amounts  = [bwt_amount])
        try:
            cert_2_epoch_1 = self.nodes[1].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_2_epoch_1) > 0)
            mark_logs("Certificate is {}".format(cert_2_epoch_1), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_2_epoch_1 in self.nodes[1].getrawmempool())
        cert2_mined = self.nodes[1].generate(1)[0]
        assert_true(cert_2_epoch_1 in self.nodes[1].getblock(cert2_mined, True)['cert'])

        self.join_network(0)

        assert_true(cert_1_epoch_1 in self.nodes[0].getblock(cert1_mined, True)['cert'])

        assert_false(cert_2_epoch_1 in self.nodes[0].getrawmempool())
        assert_true(cert_2_epoch_1 in self.nodes[1].getrawmempool())

        # node0 mines another block, therefore the remaining cert in node1 mempool will be removed since it is now
        # out of submission window 
        mined = self.nodes[0].generate(1)[0]
        self.sync_all()
       
        # not in the block just mined and not in the mempool either
        assert_false(cert_2_epoch_1 in self.nodes[1].getblock(mined, True)['cert'])
        assert_false(cert_2_epoch_1 in self.nodes[1].getrawmempool())


if __name__ == '__main__':
    quality_nodes().main()
