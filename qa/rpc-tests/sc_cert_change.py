#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, get_epoch_data, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, swap_bytes
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal
import pprint
import time

DEBUG_MODE = 1
NUMB_OF_NODES = 4
EPOCH_LENGTH = 5
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')


class sc_cert_change(BitcoinTestFramework):

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

        for k in range(0, NUMB_OF_NODES-1):
            connect_nodes_bi(self.nodes, k, k+1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        1) node0 create sidechain with 10.0 coins
           reach epoch 0
        2) node0 create a cert_ep0 for funding node1 1.0 coins
           reach epoch 1
        3) node0 create a cert_ep1 for funding node2 2.0 coins
           reach epoch 2
        4) node1 create a cert_ep2 for funding node3 3.0 coins: he uses cert_ep0 as input, change will be obtained out of a fee=0.0001
           node0 mine a new block
        5) node1 has just one UTXO with the change of cert_ep0 and it should be 0.999, it sends 0.5 to node3
           node0 mine a new block
        6) node3 has 0.5 balance and 3.0 immature from cert_ep2
        '''

        # cross chain transfer amounts
        creation_amount = Decimal("10.0")

        mark_logs("Node 0 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()

        # (1) node0 create sidechain with 10.0 coins
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

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs("Node 0 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        decoded_tx = self.nodes[0].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        mark_logs("Node0 generates 5 blocks to achieve end of epoch", self.nodes, DEBUG_MODE)
        prev_epoch_block_hash = self.nodes[0].getblockhash(self.nodes[0].getblockcount())
        self.nodes[0].generate(5)
        self.sync_all()
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_cum_tree_hash, epoch_number), self.nodes, DEBUG_MODE)

        # (2) node0 create a cert_ep0 for funding node1 1.0 coins
        addr_node1 = self.nodes[1].getnewaddress()
        bwt_amount = Decimal("1.0")
        amounts = [{"address": addr_node1, "amount": bwt_amount}]

        quality = 0
        proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node1], [bwt_amount])
        
        mark_logs("Node 0 performs a bwd transfer of {} coins to Node1 address {}".format(bwt_amount, addr_node1), self.nodes, DEBUG_MODE)
        try:
            cert_ep0 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_ep0) > 0)
            mark_logs("Certificate is {}".format(cert_ep0), self.nodes, DEBUG_MODE)
            self.sync_all()
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Node0 generates 5 blocks to achieve end of epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        # (3) node0 create a cert_ep1 for funding node1 1.0 coins
        addr_node2 = self.nodes[2].getnewaddress()
        bwt_amount = Decimal("2.0")
        amounts = [{"address": addr_node2, "amount": bwt_amount}]

        quality = 1
        proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node2], [bwt_amount])

        mark_logs("Node 0 performs a bwd transfer of {} coins to Node2 address {}".format(bwt_amount, addr_node2), self.nodes, DEBUG_MODE)
        try:
            cert_ep1 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_ep1) > 0)
            mark_logs("Certificate is {}".format(cert_ep1), self.nodes, DEBUG_MODE)
            self.sync_all()
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Node0 generates 5 blocks to achieve end of epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        # (4) node1 create a cert_ep2 for funding node3 3.0 coins: he uses cert_ep0 as input, change will be obtained out of a fee=0.0001
        addr_node3 = self.nodes[3].getnewaddress()
        bwt_amount = Decimal("3.0")
        amounts = [{"address": addr_node3, "amount": bwt_amount}]

        quality = 2
        proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node3], [bwt_amount])

        mark_logs("Node 1 performs a bwd transfer of {} coins to Node3 address {}".format(bwt_amount, addr_node3), self.nodes, DEBUG_MODE)
        try:
            cert_ep2 = self.nodes[1].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_ep2) > 0)
            mark_logs("Certificate is {}".format(cert_ep2), self.nodes, DEBUG_MODE)
            self.sync_all()
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Node0 generates 1 blocks", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # (5) node1 has just one UTXO with the change of cert_ep2 and it should be 0.999, it sends 0.5 to node3
        utxos = self.nodes[1].listunspent()
        # pprint.pprint(utxos)
        assert_equal(len(utxos), 1)
        assert_equal(utxos[0]['txid'], cert_ep2)

        taddr3 = self.nodes[3].getnewaddress()
        amount3 = Decimal("0.5")
        self.nodes[0].sendtoaddress(taddr3, amount3)
        self.sync_all()

        mark_logs("Node0 generates 1 blocks", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # (6) node3 has 0.5 balance and 3.0 immature from cert_ep2
        # the only contribution to node3 balance is the transparent fund just received
        assert_equal(self.nodes[3].getbalance(), amount3)

        try:
            res = self.nodes[3].gettransaction(cert_ep2)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Get transaction failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        # node3 has also immatureAmounts deriving from the latest certificate whose change has been used for the funds just received 
        assert_equal(res['amount'], 0.0)
        assert_equal(res['txid'], cert_ep2)
        # immature BT amounts are not displayed by default
        assert(not res['details'])

        includeWatchonly = False
        includeImmatureBTs = True

        try:
            # Specifically request also immature BT amounts
            res = self.nodes[3].gettransaction(cert_ep2, includeWatchonly, includeImmatureBTs)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Get transaction failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        # node3 has also immatureAmounts deriving from the latest certificate whose change has been used for the funds just received 
        assert_equal(res['amount'], 0.0)
        assert_equal(res['txid'], cert_ep2)
        # immature BT amounts must be displayed now
        assert_equal(res['details'][0]['amount'], bwt_amount)

if __name__ == '__main__':
    sc_cert_change().main()
