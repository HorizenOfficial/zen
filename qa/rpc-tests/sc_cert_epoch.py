#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import initialize_chain_clean, assert_equal, \
    start_nodes, stop_nodes, get_epoch_data, swap_bytes, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, mark_logs
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal
import time

DEBUG_MODE = 1
NUMB_OF_NODES = 4
EPOCH_LENGTH = 5
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = 0.0001


class sc_cert_epoch(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib', '-scproofqueuesize=0', '-logtimemicros=1', '-zapwallettxes=2']] * NUMB_OF_NODES )

        for idx, _ in enumerate(self.nodes):
            if idx < (NUMB_OF_NODES-1):
                connect_nodes_bi(self.nodes, idx, idx+1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        Node0 creates a sc, sends funds and then sends a certificate to it with a bwt to Node2
        testing maturity of bwt, spendability and balances.
        Node0 then performs some block invalidation for reverting the certificate and the tx that
        spends it. Node0 finally generates more blocks, and this causes other nodes to revert their
        chains erasing cert and tx spending it from their active chain and mempool as well
        '''

        # cross-chain transfer amount
        creation_amount = Decimal("0.5")
        fwt_amount = Decimal("30")
        fwt_amount_immature_at_epoch = Decimal("20")
        bwt_amount = Decimal("25")

        blocks = [self.nodes[0].getblockhash(0)]

        mark_logs("Node 1 generates 1 block to prepare coins to spend", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        mark_logs("Node 0 generates {} block to reach sidechain height".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(ForkHeights['MINIMAL_SC']))
        self.sync_all()

        bal_before_sc_creation = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance before SC creation: {}".format(bal_before_sc_creation), self.nodes, DEBUG_MODE)

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
            "constant": constant
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

        prev_epoch_block_hash = blocks[-1]
        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        mc_return_address = self.nodes[0].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount, "scid": scid, 'mcReturnAddress': mc_return_address}]
        fwd_tx = self.nodes[0].sc_send(cmdInput)
        mark_logs("Node 0 performs a fwd transfer of {} coins to Sc via tx {}.".format(fwt_amount, fwd_tx), self.nodes, DEBUG_MODE)
        assert(len(fwd_tx) > 0)
        self.sync_all()

        mark_logs("Node0 generates {} blocks, confirming fwd transfer".format(EPOCH_LENGTH - 2), self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(EPOCH_LENGTH - 2))
        self.sync_all()

        mc_return_address = self.nodes[0].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount_immature_at_epoch, "scid": scid, 'mcReturnAddress': mc_return_address}]
        fwd_tx = self.nodes[0].sc_send(cmdInput)

        mark_logs("Node 0 performs a fwd transfer of {} coins to Sc via tx {}.".format(fwt_amount_immature_at_epoch, fwd_tx), self.nodes, DEBUG_MODE)
        assert(len(fwd_tx) > 0)
        self.sync_all()

        mark_logs("Node0 generates 1 block moving to epoch 1 but not maturing last fw transfer", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts'][0]['amount'], fwt_amount_immature_at_epoch)

        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        addr_node2 = self.nodes[2].getnewaddress()
        amounts_bad = [{"address": addr_node2, "amount": bwt_amount + fwt_amount_immature_at_epoch}]
        amounts = [{"address": addr_node2, "amount": bwt_amount}]

        mark_logs("Node0 generating 1 block, to show cert for epoch 0 can be received within safeguard blocks", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        node1_balance_ante_cert = self.nodes[1].getbalance()
        node2_balance_ante_cert = self.nodes[2].getbalance()
        node3_balance_ante_cert = self.nodes[3].getbalance()
        mark_logs("Node 1 balance before certificate {}".format(node1_balance_ante_cert), self.nodes, DEBUG_MODE)
        mark_logs("Node 2 balance before certificate {}".format(node2_balance_ante_cert), self.nodes, DEBUG_MODE)
        mark_logs("Node 3 balance before certificate {}".format(node3_balance_ante_cert), self.nodes, DEBUG_MODE)

        mark_logs("Node 0 try to perform a bwd transfer of {} coins to Node2 address".format(bwt_amount + fwt_amount_immature_at_epoch, addr_node2), self.nodes, DEBUG_MODE)

        #Create proof for WCert
        quality = 0

        proof_bad = mcTest.create_test_proof("sc1",
                                             scid_swapped,
                                             epoch_number,
                                             quality,
                                             MBTR_SC_FEE,
                                             FT_SC_FEE,
                                             epoch_cum_tree_hash,
                                             constant = constant,
                                             pks      = [addr_node2],
                                             amounts  = [bwt_amount + fwt_amount_immature_at_epoch])

        try:
            self.nodes[0].sc_send_certificate(scid, epoch_number, quality, epoch_cum_tree_hash, proof_bad, amounts_bad, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert_equal("sidechain has insufficient funds" in errorString, True)

        #Create proof for WCert
        quality = 0
        proof = mcTest.create_test_proof("sc1",
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant,
                                         pks = [addr_node2],
                                         amounts = [bwt_amount])

        try:
            cert_epoch_0 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality, epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("Node 0 performs a bwd transfer of {} coins to Node2 address via cert {}.".format(bwt_amount, cert_epoch_0), self.nodes, DEBUG_MODE)
            assert(len(cert_epoch_0) > 0)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Node0 confims bwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        mark_logs("Checking Nodes wallet's balances are duly updated", self.nodes, DEBUG_MODE)
        assert_equal(node1_balance_ante_cert, self.nodes[1].getbalance())
        assert_equal(node2_balance_ante_cert, self.nodes[2].getbalance())  # Certificate amount is not mature yet
        assert_equal(node3_balance_ante_cert, self.nodes[3].getbalance())

        mark_logs("Checking Sc balance is duly decreased", self.nodes, DEBUG_MODE)
        sc_post_bwd = self.nodes[0].getscinfo(scid)['items'][0]
        assert_equal(sc_post_bwd["balance"], creation_amount + fwt_amount - bwt_amount)

        mark_logs("Checking that Node2 cannot immediately spend coins received from bwd transfer", self.nodes, DEBUG_MODE)
        mark_logs("Node 2 tries to send {} coins to Node3".format(bwt_amount/2), self.nodes, DEBUG_MODE)

        try:
            self.nodes[2].sendtoaddress(self.nodes[3].getnewaddress(), bwt_amount/2)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert_equal("Insufficient funds" in errorString, True)

        mark_logs("Node0 generates {} blocks, coming to the end of epoch 1".format(EPOCH_LENGTH - 2), self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(EPOCH_LENGTH - 2))
        self.sync_all()

        try:
            epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
            mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

            #Create proof for WCert
            quality = 1
            proof = mcTest.create_test_proof("sc1",
                                             scid_swapped,
                                             epoch_number,
                                             quality,
                                             MBTR_SC_FEE,
                                             FT_SC_FEE,
                                             epoch_cum_tree_hash,
                                             constant = constant)

            cert_epoch_1 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality, epoch_cum_tree_hash, proof, [], FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("Node 0 send a certificate {} with no bwd transfers".format(cert_epoch_1), self.nodes, DEBUG_MODE)
            assert(len(cert_epoch_1) > 0)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Node0 confims bwd transfer and moves at safeguard", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(2))
        self.sync_all()

        mark_logs("Checking Nodes wallet's balances are duly updated", self.nodes, DEBUG_MODE)
        assert_equal(node1_balance_ante_cert, self.nodes[1].getbalance())
        assert_equal(node2_balance_ante_cert + bwt_amount, self.nodes[2].getbalance())  # Certificate amount has finally matured
        assert_equal(node3_balance_ante_cert, self.nodes[3].getbalance())

        try:
            speding_bwd_tx = self.nodes[2].sendtoaddress(self.nodes[3].getnewaddress(), bwt_amount/2)
            mark_logs("Node 2 spends epoch 0 certificate with tx {}".format(speding_bwd_tx), self.nodes, DEBUG_MODE)
            assert(len(speding_bwd_tx) > 0)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Spending bwt founds failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()
        mark_logs("Node0 confims spending of bwd transfer founds generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        mark_logs("Checking Nodes wallet's balances are duly updated", self.nodes, DEBUG_MODE)
        assert_equal(node1_balance_ante_cert, self.nodes[1].getbalance())
        assert_equal(node2_balance_ante_cert + bwt_amount/2 + self.nodes[2].gettransaction(speding_bwd_tx)['fee'], self.nodes[2].getbalance())  # Certificate amount has finally matured
        assert_equal(node3_balance_ante_cert + bwt_amount/2, self.nodes[3].getbalance())

        ### INVALIDATION PHASE 
        mark_logs("Node0 invalidates latest block which confirmed epoch 0 bwd expenditure", self.nodes, DEBUG_MODE)
        block_to_invalidate = self.nodes[0].getbestblockhash()
        self.nodes[0].invalidateblock(block_to_invalidate)
        time.sleep(1)  # Is there a better wait to settle?

        mark_logs("Checking tx speding bwd returns to mempool", self.nodes, DEBUG_MODE)
        assert(speding_bwd_tx in self.nodes[0].getrawmempool())

        mark_logs("Node0 invalidates enough blocks to unconfirm epoch 1 certificate", self.nodes, DEBUG_MODE)
        for num in range(1,3):
            block_to_invalidate = self.nodes[0].getbestblockhash()
            self.nodes[0].invalidateblock(block_to_invalidate)
            time.sleep(1)  # Is there a better wait to settle?

        mark_logs("Checking cert returns to mempool while tx spending it gets removed from mempool", self.nodes, DEBUG_MODE)
        assert(cert_epoch_1 in self.nodes[0].getrawmempool())
        assert(speding_bwd_tx not in self.nodes[0].getrawmempool()) # speding_bwd_tx would spend an immature cert now since epoch 1 cert is not confirmed anymore

        mark_logs("Checking Sc balance is duly update due to bwd removal", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]["balance"], creation_amount + fwt_amount + fwt_amount_immature_at_epoch - bwt_amount)

        mark_logs("Node0 invalidates latest block which signaled end of epoch 1", self.nodes, DEBUG_MODE)
        block_to_invalidate = self.nodes[0].getbestblockhash()
        self.nodes[0].invalidateblock(block_to_invalidate)
        time.sleep(1)  # Is there a better wait to settle?

        mark_logs("Checking both bwd and spending tx are no more in the node mempool", self.nodes, DEBUG_MODE)
        assert(cert_epoch_1 not in self.nodes[0].getrawmempool())
        assert(speding_bwd_tx not in self.nodes[0].getrawmempool())

        mark_logs("Node0 generating 4 block to show bwd has disappeared from history", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(4))
        sc_post_regeneration = self.nodes[0].getscinfo(scid)['items'][0]
        assert_equal(sc_post_regeneration["lastCertificateEpoch"], Decimal(0))
        assert_equal(sc_post_regeneration["balance"], creation_amount + fwt_amount + fwt_amount_immature_at_epoch - bwt_amount)

        mark_logs("Node0 generating 6 block to have longest chain and cause reorg on other nodes", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(6))
        self.sync_all()

        mark_logs("Checking that upon reorg, bwd is erased from other nodes too", self.nodes, DEBUG_MODE)

        for idx, node in enumerate(self.nodes):
            mark_logs("Checking Node{} ScInfos".format(idx), self.nodes, DEBUG_MODE)
            sc_post_regeneration = node.getscinfo(scid)['items'][0]
            assert_equal(sc_post_regeneration["lastCertificateEpoch"], Decimal(0))
            assert_equal(sc_post_regeneration["balance"], creation_amount + fwt_amount + fwt_amount_immature_at_epoch - bwt_amount)
            assert(cert_epoch_1 not in node.getrawmempool())
            assert(speding_bwd_tx not in node.getrawmempool())

        assert_equal(node2_balance_ante_cert, self.nodes[2].getbalance())
        assert_equal(node3_balance_ante_cert, self.nodes[3].getbalance())

        mark_logs("Checking certificates persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        for idx, node in enumerate(self.nodes):
            mark_logs("Checking Node{} after restart".format(idx), self.nodes, DEBUG_MODE)
            sc_post_regeneration = node.getscinfo(scid)['items'][0]
            assert_equal(sc_post_regeneration["lastCertificateEpoch"], Decimal(0))
            assert_equal(sc_post_regeneration["balance"], creation_amount + fwt_amount + fwt_amount_immature_at_epoch - bwt_amount)
            assert(cert_epoch_1 not in node.getrawmempool())
            assert(speding_bwd_tx not in node.getrawmempool())


if __name__ == '__main__':
    sc_cert_epoch().main()
