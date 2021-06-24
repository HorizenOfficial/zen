#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_true, assert_false, initialize_chain_clean, \
    stop_nodes, wait_bitcoinds, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, disconnect_nodes, mark_logs, \
    dump_sc_info_record, get_epoch_data, swap_bytes
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal
import pprint
import time

NUMB_OF_NODES = 2
DEBUG_MODE = 1
SC_COINS_MAT = 2
EPOCH_LENGTH = 10
MAX_MONEY = 21000000

class SCFtAndMbtrFeesTest(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[["-sccoinsmaturity=%d" % SC_COINS_MAT, '-scproofqueuesize=0', '-logtimemicros=1', '-debug=sc',
                                              '-debug=py', '-debug=mempool', '-debug=net',
                                              '-debug=bench']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        This test checks that sidechain Forward Transfer fee, Mainchain Backward Transfer Request fee,
        and Mainchain Backward Transfer Request data length are set and handled correctly.
        '''

        # network topology: (0)--(1)

        mark_logs("Node 0 generates 220 blocks", self.nodes, DEBUG_MODE)
        blocks = self.nodes[0].generate(220)
        self.sync_all()

        mark_logs("Node 1 generates 220 blocks", self.nodes, DEBUG_MODE)
        blocks = self.nodes[1].generate(220)
        self.sync_all()

        # Sidechain parameters
        withdrawalEpochLength = EPOCH_LENGTH
        address = "dada"
        creation_amount = Decimal("50.0")
        custom_data = "bb" * 1024
        ftScFee = 10
        mbtrScFee = 20
        mbtrRequestDataLength = 1

        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk_tag = "sc1"
        vk = mcTest.generate_params(vk_tag)
        constant = generate_random_field_element_hex()
        cswVk  = ""
        feCfg = []
        bvCfg = []


        # ---------------------------------------------------------------------------------------
        # Node 1 - Create an invalid sidechain with negative FT fee
        mark_logs("\nNode 1 creates a sidechain with invalid FT fee", self.nodes, DEBUG_MODE)

        ftFee = Decimal(-ftScFee)
        mbtrFee = Decimal(mbtrScFee)

        try:
            ret = self.nodes[1].sc_create(withdrawalEpochLength, address, creation_amount, vk, custom_data, constant, cswVk, feCfg, bvCfg, ftFee, mbtrFee, mbtrRequestDataLength)
            assert_true(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)

        assert_true("Amount out of range" in errorString)


        # ---------------------------------------------------------------------------------------
        # Node 1 - Create an invalid sidechain with FT fee too high
        mark_logs("\nNode 1 creates a sidechain with FT fee too high", self.nodes, DEBUG_MODE)

        errorString = ""
        ftFee = Decimal(MAX_MONEY + 1)
        mbtrFee = Decimal(mbtrScFee)

        try:
            ret = self.nodes[1].sc_create(withdrawalEpochLength, address, creation_amount, vk, custom_data, constant, cswVk, feCfg, bvCfg, ftFee, mbtrFee, mbtrRequestDataLength)
            assert_true(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)

        assert_true("Amount out of range" in errorString)


        # ---------------------------------------------------------------------------------------
        # Node 1 - Create an invalid sidechain with negative MBTR fee
        mark_logs("\nNode 1 creates a sidechain with invalid MBTR fee", self.nodes, DEBUG_MODE)

        ftFee = Decimal(ftScFee)
        mbtrFee = Decimal(-mbtrScFee)

        try:
            ret = self.nodes[1].sc_create(withdrawalEpochLength, address, creation_amount, vk, custom_data, constant, cswVk, feCfg, bvCfg, ftFee, mbtrFee, mbtrRequestDataLength)
            assert_true(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)

        assert_true("Amount out of range" in errorString)


        # ---------------------------------------------------------------------------------------
        # Node 1 - Create an invalid sidechain with MBTR fee too high
        mark_logs("\nNode 1 creates a sidechain with MBTR fee too high", self.nodes, DEBUG_MODE)

        errorString = ""
        ftFee = Decimal(ftScFee)
        mbtrFee = Decimal(MAX_MONEY + 1)

        try:
            ret = self.nodes[1].sc_create(withdrawalEpochLength, address, creation_amount, vk, custom_data, constant, cswVk, feCfg, bvCfg, ftFee, mbtrFee, mbtrRequestDataLength)
            assert_true(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)

        assert_true("Amount out of range" in errorString)


        # ---------------------------------------------------------------------------------------
        # Node 1 - Create a valid sidechain
        mark_logs("\nNode 1 creates a new sidechain", self.nodes, DEBUG_MODE)

        errorString = ""
        ftFee = Decimal(ftScFee)
        mbtrFee = Decimal(mbtrScFee)

        try:
            ret = self.nodes[1].sc_create(withdrawalEpochLength, address, creation_amount, vk, custom_data, constant, cswVk, feCfg, bvCfg, ftFee, mbtrFee, mbtrRequestDataLength)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        creating_tx = ret['txid']
        scid = ret['scid']
        self.sync_all()

        mark_logs("Verify that FT fee, MBTR fee and MBTR request data length have been set correctly on the transaction JSON object", self.nodes, DEBUG_MODE)
        decoded_tx = self.nodes[1].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])
        assert_equal(ftFee, decoded_tx['vsc_ccout'][0]['ftScFee'])
        assert_equal(mbtrFee, decoded_tx['vsc_ccout'][0]['mbtrScFee'])
        assert_equal(mbtrRequestDataLength, decoded_tx['vsc_ccout'][0]['mbtrRequestDataLength'])

        mark_logs("Node0 generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        mark_logs("Verify that all nodes see the new sidechain", self.nodes, DEBUG_MODE)
        scinfo0 = self.nodes[0].getscinfo(scid)['items'][0]
        scinfo1 = self.nodes[1].getscinfo(scid)['items'][0]
        assert_equal(scinfo0, scinfo1)

        mark_logs("Verify that sidechain configuration is as expected", self.nodes, DEBUG_MODE)
        assert_equal(scinfo0['last ftScFee'], ftFee)
        assert_equal(scinfo0['last mbtrScFee'], mbtrFee)
        assert_equal(scinfo0['mbtrRequestDataLength'], mbtrRequestDataLength)

        self.sync_all()


        # ---------------------------------------------------------------------------------------
        # Node 1 - Check that FT outputs with amount less than sidechain FT fee are rejected
        mark_logs("\nNode 1 creates an invalid FT output with amount less than sidechain FT fee", self.nodes, DEBUG_MODE)

        errorString = ""
        ftFee = Decimal(ftScFee - 1)
        forwardTransferOuts = [{'toaddress': address, 'amount': ftFee, "scid":scid}]

        try:
            tx = self.nodes[1].send_to_sidechain(forwardTransferOuts)
            assert_true(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)

        assert_true("bad-sc-tx" in errorString)
        self.sync_all()


        # ---------------------------------------------------------------------------------------
        # Node 1 - Check that MBTR outputs with fee less than sidechain MBTR fee are rejected
        mark_logs("\nNode 1 creates an invalid MBTR output with fee less than sidechain MBTR fee", self.nodes, DEBUG_MODE)

        errorString = ""
        mbtrFee = Decimal(mbtrScFee - 1)
        fe1 = generate_random_field_element_hex()
        pkh1 = self.nodes[1].getnewaddress("", True)
        mbtrOuts = [{'vScRequestData':[fe1], 'scFee':Decimal(mbtrFee), 'scid':scid, 'pubkeyhash':pkh1 }]
        
        try:
            self.nodes[1].request_transfer_from_sidechain(mbtrOuts, {})
            assert_true(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)

        assert_true("bad-sc-tx" in errorString)
        self.sync_all()


        # ---------------------------------------------------------------------------------------
        # Node 1 - Check that FT outputs with amount equal to sidechain FT fee are rejected
        mark_logs("\nNode 1 creates an invalid FT output with amount equal to sidechain FT fee", self.nodes, DEBUG_MODE)

        errorString = ""
        ftFee = Decimal(ftScFee)
        forwardTransferOuts = [{'toaddress': address, 'amount': ftFee, "scid":scid}]

        try:
            tx = self.nodes[1].send_to_sidechain(forwardTransferOuts)
            assert_true(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)

        assert_true("bad-sc-tx" in errorString)
        self.sync_all()


        # ---------------------------------------------------------------------------------------
        # Node 1 - Check that MBTR outputs with fee equal to sidechain MBTR fee are accepted
        mark_logs("\nNode 1 creates a valid MBTR output with fee equal to sidechain MBTR fee", self.nodes, DEBUG_MODE)

        errorString = ""
        mbtrFee = Decimal(mbtrScFee)
        fe1 = generate_random_field_element_hex()
        pkh1 = self.nodes[1].getnewaddress("", True)
        mbtrOuts = [{'vScRequestData':[fe1], 'scFee':Decimal(mbtrFee), 'scid':scid, 'pubkeyhash':pkh1 }]
        
        try:
            self.nodes[1].request_transfer_from_sidechain(mbtrOuts, {})
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()


        # ---------------------------------------------------------------------------------------
        # Node 1 - Check that FT outputs with amount greater than sidechain FT fee are accepted
        mark_logs("\nNode 1 creates a valid FT output with amount greater than sidechain FT fee", self.nodes, DEBUG_MODE)

        errorString = ""
        ftFee = Decimal(ftScFee + 1)
        forwardTransferOuts = [{'toaddress': address, 'amount': ftFee, "scid":scid}]

        try:
            tx = self.nodes[1].send_to_sidechain(forwardTransferOuts)
        except JSONRPCException, e:
            errorString = e.error['message']
            print(errorString)
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()


        # ---------------------------------------------------------------------------------------
        # Node 1 - Check that MBTR outputs with fee greater than sidechain MBTR fee are accepted
        mark_logs("\nNode 1 creates a valid MBTR output with fee greater than sidechain MBTR fee", self.nodes, DEBUG_MODE)

        errorString = ""
        mbtrFee = Decimal(mbtrScFee + 1)
        fe1 = generate_random_field_element_hex()
        pkh1 = self.nodes[1].getnewaddress("", True)
        mbtrOuts = [{'vScRequestData':[fe1], 'scFee':Decimal(mbtrFee), 'scid':scid, 'pubkeyhash':pkh1 }]
        
        try:
            self.nodes[1].request_transfer_from_sidechain(mbtrOuts, {})
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()


        # ---------------------------------------------------------------------------------------
        # Node 1 - Check that a new sidechain certificate correctly updates FT and MBTR fees
        mark_logs("\nNode 1 creates a new certificate updating FT and MBTR fees", self.nodes, DEBUG_MODE)

        mark_logs("Node 1 generates " + str(EPOCH_LENGTH) + " blocks", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[1].generate(EPOCH_LENGTH))
        self.sync_all()

        mark_logs("Disconnecting nodes", self.nodes, DEBUG_MODE)
        disconnect_nodes(self.nodes[1], 0)
        disconnect_nodes(self.nodes[0], 1)
        self.is_network_split = True

        quality = 1
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[1], EPOCH_LENGTH)
        pkh_node1 = self.nodes[1].getnewaddress("", True)
        cert_amount = Decimal("10.0")
        amount_cert_1 = [{"pubkeyhash": pkh_node1, "amount": cert_amount}]

        ftFee = ftScFee
        mbtrFee = mbtrScFee
        newFtFee = ftFee + 1
        newMbtrFee = mbtrFee + 1
        scid_swapped = str(swap_bytes(scid))

        proof = mcTest.create_test_proof(
            vk_tag, scid_swapped, epoch_number, quality, newMbtrFee, newFtFee, constant, epoch_cum_tree_hash, [pkh_node1], [cert_amount])
        cert_epoch_0 = self.nodes[1].send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_1, newFtFee, newMbtrFee)

        mark_logs("Certificate sent to mempool, node 1 generates " + str(EPOCH_LENGTH / 2) + " blocks", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(EPOCH_LENGTH / 2)

        # Check that Node 1 has updated the sidechain fees
        mark_logs("\nCheck that node 1 has updated the sidechain fees", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[1].getscinfo(scid)['items'][0]['past ftScFee'], ftFee)
        assert_equal(self.nodes[1].getscinfo(scid)['items'][0]['last ftScFee'], newFtFee)
        assert_equal(self.nodes[1].getscinfo(scid)['items'][0]['past mbtrScFee'], mbtrFee)
        assert_equal(self.nodes[1].getscinfo(scid)['items'][0]['last mbtrScFee'], newMbtrFee)

        # Check that Node 0 still has the old fees (since it is not connected to node 1)
        mark_logs("\nCheck that node 0 has not updated the sidechain fees", self.nodes, DEBUG_MODE)
        tmp = self.nodes[0].getscinfo(scid)['items'][0]
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['last ftScFee'], ftFee)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['last mbtrScFee'], mbtrFee)


        # ---------------------------------------------------------------------------------------
        # Nodes 1 and 2 - Check that transactions in the mempool get checked against fees when a new certificate is published
        mark_logs("\nNode 1 creates new transactions with different fees", self.nodes, DEBUG_MODE)

        mark_logs("Node 0 creates two FTs", self.nodes, DEBUG_MODE)
        errorString = ""
        forwardTransferOuts1 = [{'toaddress': address, 'amount': newFtFee, "scid":scid}]
        forwardTransferOuts2 = [{'toaddress': address, 'amount': newFtFee + 1, "scid":scid}]

        try:
            ft_tx_1 = self.nodes[0].send_to_sidechain(forwardTransferOuts1)
            time.sleep(2)
            ft_tx_2 = self.nodes[0].send_to_sidechain(forwardTransferOuts2)
            time.sleep(2)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        mark_logs("Node 0 creates two MBTRs", self.nodes, DEBUG_MODE)
        mbtrOuts1 = [{'vScRequestData':[fe1], 'scFee':Decimal(mbtrScFee), 'scid':scid, 'pubkeyhash':pkh1 }]
        mbtrOuts2 = [{'vScRequestData':[fe1], 'scFee':Decimal(newMbtrFee), 'scid':scid, 'pubkeyhash':pkh1 }]
        
        try:
            mbtr_tx_1 = self.nodes[0].request_transfer_from_sidechain(mbtrOuts1, {})
            time.sleep(2)
            mbtr_tx_2 = self.nodes[0].request_transfer_from_sidechain(mbtrOuts2, {})
            time.sleep(2)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        mark_logs("Node 0 checks that all four transactions are in the mempool", self.nodes, DEBUG_MODE)
        assert_true(ft_tx_1 in self.nodes[0].getrawmempool())
        assert_true(ft_tx_2 in self.nodes[0].getrawmempool())
        assert_true(mbtr_tx_1 in self.nodes[0].getrawmempool())
        assert_true(mbtr_tx_2 in self.nodes[0].getrawmempool())

        mark_logs("Reconnecting nodes 0 and 1", self.nodes, DEBUG_MODE)
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 0)
        self.is_network_split = False
        sync_blocks(self.nodes[:])

        mark_logs("Node 0 checks that two transactions have been removed from mempool", self.nodes, DEBUG_MODE)
        raw_mempool = self.nodes[0].getrawmempool()
        assert_false(ft_tx_1 in raw_mempool)
        assert_true(ft_tx_2 in raw_mempool)
        assert_false(mbtr_tx_1 in raw_mempool)
        assert_true(mbtr_tx_2 in raw_mempool)

        mark_logs("Node 0 generates one block", self.nodes, DEBUG_MODE)
        last_block_hash = self.nodes[0].generate(1)[-1]
        self.sync_all()

        mark_logs("Check that the two transactions have been included in the last block", self.nodes, DEBUG_MODE)
        last_transactions = self.nodes[1].getblock(last_block_hash)['tx']
        assert_true(len(last_transactions) == 3)
        assert_true(ft_tx_2 in last_transactions)
        assert_true(mbtr_tx_2 in last_transactions)


        # ---------------------------------------------------------------------------------------
        # Check that createrawtranscation() fees and MBTR data length are set correctly
        mark_logs("\nNode 0 creates a raw transaction with sidechain creation fees", self.nodes, DEBUG_MODE)

        sc_cr = [{
            "epoch_length": withdrawalEpochLength, "amount": creation_amount, "address": address, "wCertVk": vk, "constant": constant,
            "vFieldElementCertificateFieldConfig":[], "vBitVectorCertificateFieldConfig":[],
            "forwardTransferScFee": ftFee, "mainchainBackwardTransferScFee": mbtrFee, "mainchainBackwardTransferRequestDataLength": mbtrRequestDataLength }]

        try:
            rawtx = self.nodes[0].createrawtransaction([],{},[],sc_cr)
            funded_tx = self.nodes[0].fundrawtransaction(rawtx)
            sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
            creating_tx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()

        mark_logs("Verify that FT fee, MBTR fee and MBTR request data length have been set correctly on the transaction JSON object", self.nodes, DEBUG_MODE)
        decoded_tx = self.nodes[0].getrawtransaction(creating_tx, 1)
        assert_equal(ftFee, decoded_tx['vsc_ccout'][0]['ftScFee'])
        assert_equal(mbtrFee, decoded_tx['vsc_ccout'][0]['mbtrScFee'])
        assert_equal(mbtrRequestDataLength, decoded_tx['vsc_ccout'][0]['mbtrRequestDataLength'])


        # ---------------------------------------------------------------------------------------
        # Check that create_sidechain() sets fees and MBTR data length correctly
        mark_logs("\nNode 0 creates a new sidechain with create_sidechain()", self.nodes, DEBUG_MODE)

        cmdInput = { "toaddress": address, "amount": creation_amount, 'wCertVk': vk,
                    "forwardTransferScFee": ftFee, "mainchainBackwardTransferScFee": newMbtrFee,
                    "mainchainBackwardTransferRequestDataLength": mbtrRequestDataLength }
        
        try:
            creating_tx = self.nodes[0].create_sidechain(cmdInput)['txid']
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()

        mark_logs("Verify that FT fee, MBTR fee and MBTR request data length have been set correctly on the transaction JSON object", self.nodes, DEBUG_MODE)
        decoded_tx = self.nodes[0].getrawtransaction(creating_tx, 1)
        assert_equal(ftFee, decoded_tx['vsc_ccout'][0]['ftScFee'])
        assert_equal(newMbtrFee, decoded_tx['vsc_ccout'][0]['mbtrScFee'])
        assert_equal(mbtrRequestDataLength, decoded_tx['vsc_ccout'][0]['mbtrRequestDataLength'])


        # ---------------------------------------------------------------------------------------
        # Check that FT fees are correctly checked even though the sidechain is still in mempool
        mark_logs("\nNode 0 creates a new FT transaction with invalid amount", self.nodes, DEBUG_MODE)

        scid = decoded_tx['vsc_ccout'][0]['scid']
        forwardTransferOuts = [{'toaddress': address, 'amount': ftFee, "scid":scid}]

        try:
            self.nodes[0].send_to_sidechain(forwardTransferOuts)
            assert_true(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)

        mark_logs("\nNode 0 creates a new FT transaction with valid amount", self.nodes, DEBUG_MODE)

        forwardTransferOuts = [{'toaddress': address, 'amount': newFtFee, "scid":scid}]

        try:
            self.nodes[0].send_to_sidechain(forwardTransferOuts)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)


if __name__ == '__main__':
    SCFtAndMbtrFeesTest().main()
