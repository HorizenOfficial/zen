#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import initialize_chain_clean, assert_equal, \
    start_nodes, get_epoch_data, assert_true, \
    sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
    swap_bytes, to_satoshis, disconnect_nodes
from test_framework.test_framework import ForkHeights
from test_framework.mc_test.mc_test import generate_random_field_element_hex, CertTestUtils
from decimal import Decimal
import time

DEBUG_MODE = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH = 100
CERT_FEE = Decimal("0.000123")
SC_FEE = Decimal("0.000345")
TX_FEE = Decimal("0.000567")
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
SC_COINS_MAT = 2

ORDINARY_OUTPUT = 0
TOP_QUALITY_CERT_BACKWARD_TRANSFER = 1
LOW_QUALITY_CERT_BACKWARD_TRANSFER = 2

class sc_cert_addressindex(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args= [['-blockprioritysize=0',
            '-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', 
            '-scproofqueuesize=0', '-logtimemicros=1', '-txindex', '-addressindex', f'-sccoinsmaturity={SC_COINS_MAT}']] * NUMB_OF_NODES )

        for idx, _ in enumerate(self.nodes):
            if idx < (NUMB_OF_NODES-1):
                connect_nodes_bi(self.nodes, idx, idx+1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def split_network(self):
        # Split the network of three nodes into nodes 0 and 1.
        assert not self.is_network_split
        disconnect_nodes(self.nodes[0], 1)
        disconnect_nodes(self.nodes[1], 0)
        self.is_network_split = True

    def join_network(self):
        # Join the (previously split) network pieces together: 0-1
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 0)
        # self.sync_all()
        time.sleep(2)
        self.is_network_split = False

    def run_test(self):

        mark_logs(f"Node 0 generates {ForkHeights['NON_CEASING_SC']} blocks", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['NON_CEASING_SC'])
        self.sync_all()

        # Initialize a list of pairs (sidechain version, bool)
        sidechain_versions = [
            (0, False), # Sidechain v0
            (1, False), # Sidechain v1
            (2, False), # Sidechain v2 with ceasing behavior
            (2, True)   # Sidechain v2 with non-ceasing behavior
        ]

        # Run the tests for sidechains v0, v1, and v2 (with with both ceasing and non-ceasing behavior)
        for (sidechain_version, is_non_ceasing) in sidechain_versions:
            mark_logs(f"Running test for sidechain version {sidechain_version} {'non-ceasing' if is_non_ceasing else ''}", self.nodes, DEBUG_MODE)
            self.run_test_internal(sidechain_version, is_non_ceasing)

    def run_test_internal(self, sidechain_version, is_non_ceasing=False):

        # Non-ceasing behavior is valid for sidechains v2 only
        if is_non_ceasing:
            assert_equal(sidechain_version, 2)

        #amounts
        creation_amount = Decimal("50")
        bwt_amount = Decimal("5")
        tAddr1 = self.nodes[1].getnewaddress()
        node1Addr = self.nodes[1].validateaddress(tAddr1)['address']

        # Since the test is performed in a loop for different version, we need to save the state of the node
        # at the beginning of each loop.
        current_height = self.nodes[0].getblockcount()
        starting_address_tx_ids = self.nodes[1].getaddresstxids({"addresses":[tAddr1]})
        starting_address_balance = self.nodes[1].getaddressbalance({"addresses":[tAddr1]})
        starting_address_balance_with_immature = self.nodes[1].getaddressbalance({"addresses":[tAddr1]}, True)
        starting_address_utxo = self.nodes[1].getaddressutxos({"addresses":[tAddr1]})
        starting_address_utxo_with_immature = self.nodes[1].getaddressutxos({"addresses":[tAddr1]}, True)
        starting_address_balance = self.nodes[1].getaddressbalance({"addresses":[tAddr1]})
        starting_address_balance_with_immature = self.nodes[1].getaddressbalance({"addresses":[tAddr1]}, True)

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        sc_name = f"sc{sidechain_version}-{'non-ceasable' if is_non_ceasing else ''}"
        vk = mcTest.generate_params(sc_name)
        constant = generate_random_field_element_hex()

        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': 0 if is_non_ceasing else EPOCH_LENGTH,
            'toaddress': "dada",
            'amount': creation_amount,
            'wCertVk': vk,
            'constant': constant
        }

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs(f"Node 1 created the SC spending {creation_amount} coins via tx {creating_tx}.", self.nodes, DEBUG_MODE)
        self.sync_all()

        decoded_tx = self.nodes[0].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])
        mark_logs(f"created SC id: {scid}", self.nodes, DEBUG_MODE)

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        sc_creation_block_hash = self.nodes[0].generate(1)[0]
        current_height += 1
        sc_creation_block = self.nodes[0].getblock(sc_creation_block_hash)
        self.sync_all()

        #Advance for 1 Epoch
        mark_logs("Advance for 1 Epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(EPOCH_LENGTH)
        current_height += EPOCH_LENGTH
        self.sync_all()

        #Mine Certificate 1 with quality = 5
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        quality = 0 if is_non_ceasing else 5
        proof = mcTest.create_test_proof(
            sc_name, scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount])

        amount_cert_1 = [{"address": node1Addr, "amount": bwt_amount}]

        mark_logs(f"Mine Certificate 1 with quality = {quality}...", self.nodes, DEBUG_MODE)

        total_bwt_amount = 0 # This is a total amount needed for non-ceasing sidechain testing

        cert1 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)

        total_bwt_amount += bwt_amount

        self.sync_all()
        
        maturityHeight = sc_creation_block["height"] + (EPOCH_LENGTH * 2) + EPOCH_LENGTH // 5 - 1

        ####### Test getaddressmempool ########
        addressmempool = self.nodes[1].getaddressmempool({"addresses":[tAddr1]})
        assert_equal(len(addressmempool),1)
        assert_equal(addressmempool[0]['txid'], cert1)
        assert_equal(addressmempool[0]['satoshis'], float(bwt_amount) * 1e8)
        assert_equal(addressmempool[0]['address'], tAddr1)
        assert_equal(addressmempool[0]['outstatus'], TOP_QUALITY_CERT_BACKWARD_TRANSFER)

        self.nodes[0].generate(1)
        current_height += 1
        self.sync_all()

        ####### Test getaddressmempool ########
        addressmempool = self.nodes[1].getaddressmempool({"addresses":[tAddr1]})
        assert_equal(addressmempool, [])
        ####### Test getaddresstxids ########
        addresstxids = self.nodes[1].getaddresstxids({"addresses":[tAddr1]})
        assert_equal(len(addresstxids), len(starting_address_tx_ids) + 1)
        assert_equal(addresstxids[-1], cert1)
        ####### Test getaddressbalance ########
        addressbalance = self.nodes[1].getaddressbalance({"addresses":[tAddr1]})
        addressbalanceWithImmature = self.nodes[1].getaddressbalance({"addresses":[tAddr1]}, True)

        if is_non_ceasing:
            assert_equal(addressbalance["balance"], starting_address_balance["balance"] + to_satoshis(bwt_amount))
            assert_equal(addressbalance["immature"], starting_address_balance["immature"] + 0)
            assert_equal(addressbalance["received"], starting_address_balance["received"] + to_satoshis(bwt_amount))
            assert_equal(addressbalanceWithImmature["balance"], starting_address_balance_with_immature["balance"] + to_satoshis(bwt_amount))
            assert_equal(addressbalanceWithImmature["immature"], starting_address_balance_with_immature["immature"] + 0)
            assert_equal(addressbalanceWithImmature["received"], starting_address_balance_with_immature["received"] + to_satoshis(bwt_amount))
        else:
            assert_equal(addressbalance["balance"], starting_address_balance["balance"] + 0)
            assert_equal(addressbalance["immature"], starting_address_balance["immature"] + to_satoshis(bwt_amount))
            assert_equal(addressbalance["received"], starting_address_balance["received"] + 0)
            assert_equal(addressbalanceWithImmature["balance"], starting_address_balance_with_immature["balance"] + to_satoshis(bwt_amount))
            assert_equal(addressbalanceWithImmature["immature"], starting_address_balance_with_immature["immature"] + to_satoshis(bwt_amount))
            assert_equal(addressbalanceWithImmature["received"], starting_address_balance_with_immature["received"] + to_satoshis(bwt_amount))
        ####### Test getaddressutxo ########
        addressutxo = self.nodes[1].getaddressutxos({"addresses":[tAddr1]})
        assert_equal(len(addressutxo), len(starting_address_utxo) + 1 if is_non_ceasing else len(starting_address_utxo))
        addressutxoWithImmature = self.nodes[1].getaddressutxos({"addresses":[tAddr1]}, True)
        assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 1)
        assert_true(addressutxoWithImmature[-1]["backwardTransfer"])
        assert_equal(addressutxoWithImmature[-1]["mature"], is_non_ceasing)
        assert_equal(addressutxoWithImmature[-1]["maturityHeight"], current_height if is_non_ceasing else maturityHeight)
        assert_equal(addressutxoWithImmature[-1]["satoshis"], to_satoshis(bwt_amount))
        assert_equal(addressutxoWithImmature[-1]["blocksToMaturity"], 0 if is_non_ceasing else maturityHeight - current_height)

        #Add to mempool Certificate 2 with quality = 7
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH, is_non_ceasing, self.nodes[0].getblockcount() - 1)
        quality = 0 if is_non_ceasing else 7
        bwt_amount2 = Decimal("7")
        mark_logs(f"Add to mempool Certificate 2 with quality = {quality}...", self.nodes, DEBUG_MODE)
        proof = mcTest.create_test_proof(
            sc_name, scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount2])

        amount_cert_2 = [{"address": node1Addr, "amount": bwt_amount2}]

        cert2 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_2, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)

        total_bwt_amount += bwt_amount2

        self.sync_all()

        ####### Test getaddressmempool ########
        addressmempool = self.nodes[1].getaddressmempool({"addresses":[tAddr1]})
        assert_equal(len(addressmempool), 1)
        assert_equal(addressmempool[0]['txid'], cert2)
        assert_equal(addressmempool[0]['satoshis'], float(bwt_amount2) * 1e8)
        assert_equal(addressmempool[0]['address'], tAddr1)
        assert_equal(addressmempool[0]['outstatus'], TOP_QUALITY_CERT_BACKWARD_TRANSFER)

        quality = 0 if is_non_ceasing else 9
        bwt_amount3 = Decimal("9")
        mark_logs(f"Add to mempool Certificate 3 with quality = {quality}...", self.nodes, DEBUG_MODE)

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH, is_non_ceasing, self.nodes[0].getblockcount())
        if is_non_ceasing:
            epoch_number += 1 # TODO: how to handle unconfirmed epoch due to certificates in the mempool? Maybe with an "unconfEpoch" field...

        proof = mcTest.create_test_proof(
            sc_name, scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount3])

        amount_cert_3 = [{"address": node1Addr, "amount": bwt_amount3}]

        cert3 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_3, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)

        total_bwt_amount += bwt_amount3

        self.sync_all()

        ####### Test getaddressmempool ########
        addressmempool = self.nodes[1].getaddressmempool({"addresses":[tAddr1]})
        assert_equal(len(addressmempool), 2)
        assert_equal(addressmempool[0]['txid'], cert2)
        assert_equal(addressmempool[0]['satoshis'], float(bwt_amount2) * 1e8)
        assert_equal(addressmempool[0]['address'], tAddr1)
        assert_equal(addressmempool[0]['outstatus'], LOW_QUALITY_CERT_BACKWARD_TRANSFER) # TODO: check this, it seems wrong for non-ceasing sidechains

        assert_equal(addressmempool[1]['txid'], cert3)
        assert_equal(addressmempool[1]['satoshis'], float(bwt_amount3) * 1e8)
        assert_equal(addressmempool[1]['address'], tAddr1)
        assert_equal(addressmempool[1]['outstatus'], TOP_QUALITY_CERT_BACKWARD_TRANSFER)

        self.nodes[0].generate(1)
        current_height += 1
        self.sync_all()

        ####### Test getaddresstxids ########
        addresstxids = self.nodes[1].getaddresstxids({"addresses":[tAddr1]})
        assert_equal(len(addresstxids), len(starting_address_tx_ids) + 3)
        assert_true(cert1 in addresstxids and cert2 in addresstxids and cert3 in addresstxids)
        ####### Test getaddressmempool ########
        addressmempool = self.nodes[1].getaddressmempool({"addresses":[tAddr1]})
        assert_equal(len(addressmempool), 0)
        ####### Test getaddressbalance ########
        addressbalance = self.nodes[1].getaddressbalance({"addresses":[tAddr1]})
        addressbalanceWithImmature = self.nodes[1].getaddressbalance({"addresses":[tAddr1]}, True)

        if is_non_ceasing:
            assert_equal(addressbalance["balance"], starting_address_balance["balance"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalance["immature"], starting_address_balance["immature"] + 0)
            assert_equal(addressbalance["received"], starting_address_balance["received"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalanceWithImmature["balance"], starting_address_balance_with_immature["balance"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalanceWithImmature["immature"], starting_address_balance_with_immature["immature"] + 0)
            assert_equal(addressbalanceWithImmature["received"], starting_address_balance_with_immature["received"] + to_satoshis(total_bwt_amount))
        else:
            assert_equal(addressbalance["balance"], starting_address_balance["balance"] + 0)
            assert_equal(addressbalance["immature"], starting_address_balance["immature"] + to_satoshis(bwt_amount3))
            assert_equal(addressbalance["received"], starting_address_balance["received"] + 0)
            assert_equal(addressbalanceWithImmature["balance"], starting_address_balance_with_immature["balance"] + to_satoshis(bwt_amount3))
            assert_equal(addressbalanceWithImmature["immature"], starting_address_balance_with_immature["immature"] + to_satoshis(bwt_amount3))
            assert_equal(addressbalanceWithImmature["received"], starting_address_balance_with_immature["received"] + to_satoshis(bwt_amount3))
        ####### Test getaddressutxo ########
        addressutxo = self.nodes[1].getaddressutxos({"addresses":[tAddr1]})
        addressutxoWithImmature = self.nodes[1].getaddressutxos({"addresses":[tAddr1]}, True)

        if is_non_ceasing:
            # Take the last 3 certificate UTXOs
            address_utxo_slice = addressutxoWithImmature[len(addressutxoWithImmature) - 3:]
            # addressutxoWithImmature RPC command doesn't return the UTXOs in
            # chronological order, so we need to sort them by amount (because
            # we sent the certificates with increasing BT amounts).
            address_utxo_slice.sort(key=lambda x: x["satoshis"])
            # The first certificate was included (and matured) in the penultimate block
            # while the last two were included (and matured) in the last one
            maturity_heights = [current_height - 1, current_height, current_height]
            bwt_amounts = [bwt_amount, bwt_amount2, bwt_amount3]
        else:
            # Take the top quality certificate UTXO only
            address_utxo_slice = addressutxoWithImmature[len(addressutxoWithImmature) - 1:]
            maturity_heights = [maturityHeight]
            bwt_amounts = [bwt_amount3]

        if is_non_ceasing:
            # For non-ceasing sidechains, all the 3 certs are "valid", there is no superseding
            assert_equal(len(addressutxo), len(starting_address_utxo) + 3)
            assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 3)
        else:
            # For ceasing sidechains, only the top quality UTXO is "valid" and it's not yet matured
            assert_equal(len(addressutxo), len(starting_address_utxo) + 0)
            assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 1)

        # Check the status of certificate UTXOs (only 1 for ceasing sidechains, 3 for non-ceasing ones)
        for index, address_utxo in enumerate(address_utxo_slice):
            assert_true(address_utxo["backwardTransfer"])
            assert_equal(address_utxo["mature"], is_non_ceasing)
            assert_equal(address_utxo["maturityHeight"], maturity_heights[index])
            assert_equal(address_utxo["satoshis"], to_satoshis(bwt_amounts[index]))
            assert_equal(address_utxo["blocksToMaturity"], 0 if is_non_ceasing else maturityHeight - current_height)

        # Generate another block to make room for the next two certificates
        # (this applies only to non-ceasing sidechains as they need to
        # reference different blocks).
        self.nodes[0].generate(1)
        current_height += 1
        self.sync_all()

        # Split the network: (0) / (1)
        mark_logs("\nSplit network", self.nodes, DEBUG_MODE)
        self.split_network()
        mark_logs("The network is split: 0 / 1", self.nodes, DEBUG_MODE)


        #Mine a block with Certificate 4 with quality = 11 and Certificate 5 with quality = 13
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH, is_non_ceasing, current_height - 1)
        quality = 0 if is_non_ceasing else 11
        bwt_amount4 = Decimal("11")
        mark_logs(f"Create a Certificate 4 with quality = {quality}...", self.nodes, DEBUG_MODE)
        proof = mcTest.create_test_proof(
            sc_name, scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount4])

        amount_cert_4 = [{"address": node1Addr, "amount": bwt_amount4}]

        cert4 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_4, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)

        total_bwt_amount += bwt_amount4

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH, is_non_ceasing, current_height)
        if is_non_ceasing:
            epoch_number += 1
        quality = 0 if is_non_ceasing else 13
        bwt_amount5 = Decimal("13")
        mark_logs(f"Create a Certificate 5 with quality = {quality}...", self.nodes, DEBUG_MODE)
        proof = mcTest.create_test_proof(
            sc_name, scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount5])

        amount_cert_5 = [{"address": node1Addr, "amount": bwt_amount5}]

        cert5 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_5, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)

        total_bwt_amount += bwt_amount5

        lastBlock = self.nodes[0].generate(1)[0]
        current_height += 1

        # Checking the network chain tips
        mark_logs("\nChecking network chain tips...", self.nodes, DEBUG_MODE)
        print(self.nodes[0].getblockchaininfo()['blocks'])
        print(self.nodes[1].getblockchaininfo()['blocks'])

        assert_equal(self.nodes[0].getblockchaininfo()['blocks'], current_height)
        assert_equal(self.nodes[1].getblockchaininfo()['blocks'], current_height - 1)

        mark_logs("\nJoining network", self.nodes, DEBUG_MODE)
        self.join_network()
        mark_logs("\nNetwork joined", self.nodes, DEBUG_MODE)

        ####### Test getaddresstxids ########
        addresstxids = self.nodes[1].getaddresstxids({"addresses":[tAddr1]})
        assert_equal(len(addresstxids), len(starting_address_tx_ids) + 5)
        ####### Test getaddressmempool ########
        addressmempool = self.nodes[1].getaddressmempool({"addresses":[tAddr1]})
        assert_equal(len(addressmempool), 0)
        ####### Test getaddressbalance ########
        addressbalance = self.nodes[1].getaddressbalance({"addresses":[tAddr1]})
        addressbalanceWithImmature = self.nodes[1].getaddressbalance({"addresses":[tAddr1]}, True)

        if is_non_ceasing:
            assert_equal(addressbalance["balance"], starting_address_balance["balance"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalance["immature"], starting_address_balance["immature"] + 0)
            assert_equal(addressbalance["received"], starting_address_balance["received"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalanceWithImmature["balance"], starting_address_balance_with_immature["balance"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalanceWithImmature["immature"], starting_address_balance_with_immature["immature"] + 0)
            assert_equal(addressbalanceWithImmature["received"], starting_address_balance_with_immature["received"] + to_satoshis(total_bwt_amount))
        else:
            assert_equal(addressbalance["balance"], starting_address_balance["balance"] + 0)
            assert_equal(addressbalance["immature"], starting_address_balance["immature"] + to_satoshis(bwt_amount5))
            assert_equal(addressbalance["received"], starting_address_balance["received"] + 0)
            assert_equal(addressbalanceWithImmature["balance"], starting_address_balance_with_immature["balance"] + to_satoshis(bwt_amount5))
            assert_equal(addressbalanceWithImmature["immature"], starting_address_balance_with_immature["immature"] + to_satoshis(bwt_amount5))
            assert_equal(addressbalanceWithImmature["received"], starting_address_balance_with_immature["received"] + to_satoshis(bwt_amount5))
        ####### Test getaddressutxo ########

        addressutxo = self.nodes[1].getaddressutxos({"addresses":[tAddr1]})
        addressutxoWithImmature = self.nodes[1].getaddressutxos({"addresses":[tAddr1]}, True)

        if is_non_ceasing:
            # For non-ceasing sidechains all the submitted certificates have "valid" UTXOs
            assert_equal(len(addressutxo), len(starting_address_utxo) + 5)
            assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 5)
        else:
            assert_equal(len(addressutxo), len(starting_address_utxo) + 0)
            assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 1)

        if is_non_ceasing:
            # Take the last 2 certificate UTXOs
            address_utxo_slice = addressutxoWithImmature[len(addressutxoWithImmature) - 2:]
            address_utxo_slice.sort(key=lambda x: x["satoshis"])
            bwt_amounts = [bwt_amount4, bwt_amount5]
        else:
            # Take the top quality certificate UTXO only
            address_utxo_slice = addressutxoWithImmature[len(addressutxoWithImmature) - 1:]
            maturity_heights = [maturityHeight]
            bwt_amounts = [bwt_amount5]

        if is_non_ceasing:
            # For non-ceasing sidechains, all the last 5 certs are "valid", there is no superseding
            assert_equal(len(addressutxo), len(starting_address_utxo) + 5)
            assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 5)
        else:
            # For ceasing sidechains, only the top quality UTXO is "valid" and it's not yet matured
            assert_equal(len(addressutxo), len(starting_address_utxo) + 0)
            assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 1)

        # Check the status of certificate UTXOs (only 1 for ceasing sidechains, 2 for non-ceasing ones)
        for index, address_utxo in enumerate(address_utxo_slice):
            assert_true(address_utxo["backwardTransfer"])
            assert_equal(address_utxo["mature"], is_non_ceasing)
            assert_equal(address_utxo["maturityHeight"], current_height if is_non_ceasing else maturityHeight)
            assert_equal(address_utxo["satoshis"], to_satoshis(bwt_amounts[index]))
            assert_equal(address_utxo["blocksToMaturity"], 0 if is_non_ceasing else maturityHeight - current_height)


        # Checking the network chain tips
        mark_logs("\nChecking network chain tips...", self.nodes, DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            assert_equal(self.nodes[i].getblockchaininfo()['blocks'], current_height)

        mark_logs("\nInvalidating the last block and checking RPC call results...", self.nodes, DEBUG_MODE)
        self.nodes[1].invalidateblock(lastBlock)
        current_height -= 1
        total_bwt_amount -= bwt_amount4 + bwt_amount5
        ####### Test getaddressmempool ########
        addressmempool = self.nodes[1].getaddressmempool({"addresses":[tAddr1]})
        assert_equal(len(addressmempool), 2)

        for address_mempool_entry in addressmempool:
            if (address_mempool_entry['txid'] == cert4):
                assert_equal(address_mempool_entry['txid'], cert4)
                assert_equal(address_mempool_entry['satoshis'], float(bwt_amount4) * 1e8)
                assert_equal(address_mempool_entry['address'], tAddr1)
                assert_equal(address_mempool_entry['outstatus'], LOW_QUALITY_CERT_BACKWARD_TRANSFER)
            else:
                assert_equal(address_mempool_entry['txid'], cert5)
                assert_equal(address_mempool_entry['satoshis'], float(bwt_amount5) * 1e8)
                assert_equal(address_mempool_entry['address'], tAddr1)
                assert_equal(address_mempool_entry['outstatus'], TOP_QUALITY_CERT_BACKWARD_TRANSFER)

        ####### Test getaddressbalance ########
        addressbalance = self.nodes[1].getaddressbalance({"addresses":[tAddr1]})
        addressbalanceWithImmature = self.nodes[1].getaddressbalance({"addresses":[tAddr1]}, True)

        if is_non_ceasing:
            assert_equal(addressbalance["balance"], starting_address_balance["balance"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalance["immature"], starting_address_balance["immature"] + 0)
            assert_equal(addressbalance["received"], starting_address_balance["received"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalanceWithImmature["balance"], starting_address_balance_with_immature["balance"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalanceWithImmature["immature"], starting_address_balance_with_immature["immature"] + 0)
            assert_equal(addressbalanceWithImmature["received"], starting_address_balance_with_immature["received"] + to_satoshis(total_bwt_amount))
        else:
            assert_equal(addressbalance["balance"], starting_address_balance["balance"] + 0)
            assert_equal(addressbalance["immature"], starting_address_balance["immature"] + to_satoshis(bwt_amount3))
            assert_equal(addressbalance["received"], starting_address_balance["received"] + 0)
            assert_equal(addressbalanceWithImmature["balance"], starting_address_balance_with_immature["balance"] + to_satoshis(bwt_amount3))
            assert_equal(addressbalanceWithImmature["immature"], starting_address_balance_with_immature["immature"] + to_satoshis(bwt_amount3))
            assert_equal(addressbalanceWithImmature["received"], starting_address_balance_with_immature["received"] + to_satoshis(bwt_amount3))
        ####### Test getaddressutxo ########
        addressutxo = self.nodes[1].getaddressutxos({"addresses":[tAddr1]})
        addressutxoWithImmature = self.nodes[1].getaddressutxos({"addresses":[tAddr1]}, True)

        if is_non_ceasing:
            # Take the last 3 certificate UTXOs
            address_utxo_slice = addressutxoWithImmature[len(addressutxoWithImmature) - 3:]
            # addressutxoWithImmature RPC command doesn't return the UTXOs in
            # chronological order, so we need to sort them by amount (because
            # we sent the certificates with increasing BT amounts).
            address_utxo_slice.sort(key=lambda x: x["satoshis"])
            # Let's calculate the maturity heights:
            # Block N-2 => Cert1
            # Block N-1 => Cert2 and Cert3
            # Block N => None
            # Block N+1 => Cert4 and Cert5
            # Then we reverted block N+1
            maturity_heights = [current_height - 2, current_height - 1, current_height - 1]
            bwt_amounts = [bwt_amount, bwt_amount2, bwt_amount3]
        else:
            # Take the top quality certificate UTXO only
            address_utxo_slice = addressutxoWithImmature[len(addressutxoWithImmature) - 1:]
            maturity_heights = [maturityHeight]
            bwt_amounts = [bwt_amount3]

        if is_non_ceasing:
            # For non-ceasing sidechains, all the 3 certs are "valid", there is no superseding
            assert_equal(len(addressutxo), len(starting_address_utxo) + 3)
            assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 3)
        else:
            # For ceasing sidechains, only the top quality UTXO is "valid" and it's not yet matured
            assert_equal(len(addressutxo), len(starting_address_utxo) + 0)
            assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 1)

        # Check the status of certificate UTXOs (only 1 for ceasing sidechains, 3 for non-ceasing ones)
        for index, address_utxo in enumerate(address_utxo_slice):
            assert_true(address_utxo["backwardTransfer"])
            assert_equal(address_utxo["mature"], is_non_ceasing)
            assert_equal(address_utxo["maturityHeight"], maturity_heights[index])
            assert_equal(address_utxo["satoshis"], to_satoshis(bwt_amounts[index]))
            assert_equal(address_utxo["blocksToMaturity"], 0 if is_non_ceasing else maturityHeight - current_height)


        # Generate blocks to reach maturity height (and also make sidechain cease)
        mark_logs("\nGenerating blocks to make the sidechain ceased...", self.nodes, DEBUG_MODE)
        lastBlock = self.nodes[1].generate(maturityHeight - current_height)[-1]
        certificates_inclusion_height = current_height + 1
        current_height = maturityHeight
        total_bwt_amount += bwt_amount4 + bwt_amount5
        self.sync_all()

        mark_logs("Checking that all the certificates are considered as not mature...", self.nodes, DEBUG_MODE)
        ####### Test getaddressbalance ########
        addressbalance = self.nodes[1].getaddressbalance({"addresses":[tAddr1]})
        addressbalanceWithImmature = self.nodes[1].getaddressbalance({"addresses":[tAddr1]}, True)
        # All the balances should be 0 since the sidechain is ceased and no BT has matured
        if is_non_ceasing:
            assert_equal(addressbalance["balance"], starting_address_balance["balance"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalance["immature"], starting_address_balance["immature"] + 0)
            assert_equal(addressbalance["received"], starting_address_balance["received"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalanceWithImmature["balance"], starting_address_balance_with_immature["balance"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalanceWithImmature["immature"], starting_address_balance_with_immature["immature"] + 0)
            assert_equal(addressbalanceWithImmature["received"], starting_address_balance_with_immature["received"] + to_satoshis(total_bwt_amount))
        else:
            assert_equal(addressbalance["balance"], starting_address_balance["balance"] + 0)
            assert_equal(addressbalance["immature"], starting_address_balance["immature"] + 0)
            assert_equal(addressbalance["received"], starting_address_balance["received"] + 0)
            assert_equal(addressbalanceWithImmature["balance"], starting_address_balance_with_immature["balance"] + 0)
            assert_equal(addressbalanceWithImmature["immature"], starting_address_balance_with_immature["immature"] + 0)
            assert_equal(addressbalanceWithImmature["received"], starting_address_balance_with_immature["received"] + 0)
        ####### Test getaddressutxo ########
        addressutxo = self.nodes[1].getaddressutxos({"addresses":[tAddr1]})
        addressutxoWithImmature = self.nodes[1].getaddressutxos({"addresses":[tAddr1]}, True)

        if is_non_ceasing:
            assert_equal(len(addressutxo), len(starting_address_utxo) + 5)
            assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 5)
        else:
            assert_equal(len(addressutxo), len(starting_address_utxo) + 0)
            assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 0)

        # Invalidate the last block to recover the sidechain from the "ceased" state and make
        # it "alive" again.
        mark_logs("\nInvalidating the last block to make the sidechain alive again...", self.nodes, DEBUG_MODE)
        self.nodes[0].invalidateblock(lastBlock)
        self.nodes[1].invalidateblock(lastBlock)
        current_height -= 1

        mark_logs("Checking that the certificates are restored as they were before the block revert...", self.nodes, DEBUG_MODE)
        ####### Test getaddressbalance ########
        addressbalance = self.nodes[1].getaddressbalance({"addresses":[tAddr1]})
        addressbalanceWithImmature = self.nodes[1].getaddressbalance({"addresses":[tAddr1]}, True)

        if is_non_ceasing:
            assert_equal(addressbalance["balance"], starting_address_balance["balance"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalance["immature"], starting_address_balance["immature"] + 0)
            assert_equal(addressbalance["received"], starting_address_balance["received"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalanceWithImmature["balance"], starting_address_balance_with_immature["balance"] + to_satoshis(total_bwt_amount))
            assert_equal(addressbalanceWithImmature["immature"], starting_address_balance_with_immature["immature"] + 0)
            assert_equal(addressbalanceWithImmature["received"], starting_address_balance_with_immature["received"] + to_satoshis(total_bwt_amount))
        else:
            assert_equal(addressbalance["balance"], starting_address_balance["balance"] + 0)
            assert_equal(addressbalance["immature"], starting_address_balance["immature"] + to_satoshis(bwt_amount5))
            assert_equal(addressbalance["received"], starting_address_balance["received"] + 0)
            assert_equal(addressbalanceWithImmature["balance"], starting_address_balance_with_immature["balance"] + to_satoshis(bwt_amount5))
            assert_equal(addressbalanceWithImmature["immature"], starting_address_balance_with_immature["immature"] + to_satoshis(bwt_amount5))
            assert_equal(addressbalanceWithImmature["received"], starting_address_balance_with_immature["received"] + to_satoshis(bwt_amount5))
        ####### Test getaddressutxo ########

        addressutxo = self.nodes[1].getaddressutxos({"addresses":[tAddr1]})
        addressutxoWithImmature = self.nodes[1].getaddressutxos({"addresses":[tAddr1]}, True)

        if is_non_ceasing:
            # For non-ceasing sidechains all the submitted certificates have "valid" UTXOs
            assert_equal(len(addressutxo), len(starting_address_utxo) + 5)
            assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 5)
        else:
            assert_equal(len(addressutxo), len(starting_address_utxo) + 0)
            assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 1)

        if is_non_ceasing:
            # Take the last 2 certificate UTXOs
            address_utxo_slice = addressutxoWithImmature[len(addressutxoWithImmature) - 2:]
            address_utxo_slice.sort(key=lambda x: x["satoshis"])
            bwt_amounts = [bwt_amount4, bwt_amount5]
        else:
            # Take the top quality certificate UTXO only
            address_utxo_slice = addressutxoWithImmature[len(addressutxoWithImmature) - 1:]
            maturity_heights = [maturityHeight]
            bwt_amounts = [bwt_amount5]

        if is_non_ceasing:
            # For non-ceasing sidechains, all the last 5 certs are "valid", there is no superseding
            assert_equal(len(addressutxo), len(starting_address_utxo) + 5)
            assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 5)
        else:
            # For ceasing sidechains, only the top quality UTXO is "valid" and it's not yet matured
            assert_equal(len(addressutxo), len(starting_address_utxo) + 0)
            assert_equal(len(addressutxoWithImmature), len(starting_address_utxo_with_immature) + 1)

        # Check the status of certificate UTXOs (only 1 for ceasing sidechains, 2 for non-ceasing ones)
        for index, address_utxo in enumerate(address_utxo_slice):
            assert_true(address_utxo["backwardTransfer"])
            assert_equal(address_utxo["mature"], is_non_ceasing)
            assert_equal(address_utxo["maturityHeight"], certificates_inclusion_height if is_non_ceasing else maturityHeight)
            assert_equal(address_utxo["satoshis"], to_satoshis(bwt_amounts[index]))
            assert_equal(address_utxo["blocksToMaturity"], 0 if is_non_ceasing else maturityHeight - current_height)

        ret = self.nodes[1].verifychain(4, 0)
        assert_equal(ret, True)


if __name__ == '__main__':
    sc_cert_addressindex().main()
