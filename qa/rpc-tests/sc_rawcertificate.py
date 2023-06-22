#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_equal, initialize_chain_clean, \
    get_epoch_data, swap_bytes, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, p2p_port, mark_logs
from test_framework.mc_test.mc_test import *
import os
from decimal import *
import operator
import pprint
from random import randrange

import time

DEBUG_MODE = 1
EPOCH_LENGTH = 5
NUMB_OF_NODES = 4
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal("0.000135")

class sc_rawcert(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-scproofqueuesize=0', '-logtimemicros=1', '-txindex=1', '-zapwallettxes=2']] * NUMB_OF_NODES)

        for idx, _ in enumerate(self.nodes):
            if idx < (NUMB_OF_NODES - 1):
                connect_nodes_bi(self.nodes, idx, idx + 1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        def get_spendable(nodeIdx, min_amount):
            # get a UTXO for setting fee
            utx = False
            listunspent = self.nodes[nodeIdx].listunspent()
            for aUtx in listunspent:
                if aUtx['amount'] > min_amount:
                    utx = aUtx
                    change = aUtx['amount'] - min_amount
                    break
 
            if utx == False:
                pprint.pprint(listunspent)

            assert_equal(utx!=False, True)
            return utx, change

        '''
        Testing the capabilities of the api for creating raw certificates and handling their decoding.
        Negative tests are also performed by specifying wrong params and incorrect pkey for the signing
        '''

        # forward transfer amount
        cr_amount = Decimal("5.0")
        bt_amount = Decimal("4.0")
        sc_amount = cr_amount 

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs("Node 3 generates {} block".format(ForkHeights['MINIMAL_SC']-1), self.nodes, DEBUG_MODE)
        self.nodes[3].generate(ForkHeights['MINIMAL_SC'] - 1)
        self.sync_all()

        # node 1 has just the coinbase which is now mature
        bal_before = self.nodes[1].getbalance("", 0)

        # create a sc via createraw cmd
        mark_logs("Node 1 creates the SC spending " + str(sc_amount) + " coins ...", self.nodes, DEBUG_MODE)
        sc_address = "fade"

        #generate vk and constant for this sidechain
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()
        
        sc_cr = [{
            "version": 0,
            "epoch_length": EPOCH_LENGTH,
            "amount": cr_amount,
            "address": sc_address,
            "wCertVk": vk,
            "constant": constant
        }]
        sc_ft = []
        raw_tx = self.nodes[1].createrawtransaction([], {}, [], sc_cr, sc_ft)
        funded_tx = self.nodes[1].fundrawtransaction(raw_tx)
        signed_tx = self.nodes[1].signrawtransaction(funded_tx['hex'])
        creating_tx = self.nodes[1].sendrawtransaction(signed_tx['hex'])
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(creating_tx, 1)
        scid = decoded_tx['vsc_ccout'][0]['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        #retrieve previous_end_epoch_mc_b_hash
        current_height = self.nodes[3].getblockcount()
        mark_logs("Node3 generating {} blocks".format(EPOCH_LENGTH), self.nodes, DEBUG_MODE)
        self.nodes[3].generate(EPOCH_LENGTH)
        self.sync_all()

        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        # save them for the last test
        epn_0 = epoch_number
        epoch_cum_tree_hash_0 = epoch_cum_tree_hash

        # -------------------------- end epoch

        sc_funds_pre = self.nodes[3].getscinfo(scid)['items'][0]['balance']

        addr_node2 = self.nodes[2].getnewaddress()

        mark_logs("Node3 generating 2 block, overcoming safeguard", self.nodes, DEBUG_MODE)
        self.nodes[3].generate(2)
        self.sync_all()

        # create wCert proof
        quality = 0
        proof = mcTest.create_test_proof("sc1",
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant,
                                         pks      = [addr_node2],
                                         amounts  = [bt_amount])

        utx, change = get_spendable(0, CERT_FEE)
        raw_inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        raw_outs    = { self.nodes[0].getnewaddress() : change }

        raw_bwt_outs = [{"address":addr_node2, "amount":bt_amount}]
        raw_params = {
            "scid": scid,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash,
            "scProof": proof,
            "withdrawalEpochNumber": epoch_number
        }
        raw_cert = []
        cert = []

        try:
            raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[0].signrawtransaction(raw_cert)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)
            assert_true(False)

        decoded_cert_pre = self.nodes[0].decoderawtransaction(raw_cert)
        decoded_cert_pre_list = sorted(decoded_cert_pre.items())

        mark_logs("Node0 sending raw certificate for epoch {}, expecting failure...".format(epoch_number), self.nodes, DEBUG_MODE)
        # we expect it to fail because beyond the safeguard
        try:
            cert = self.nodes[0].sendrawtransaction(signed_cert['hex'])
            assert_true(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("======> ", errorString, "\n")

        mark_logs("Node0 invalidates last block, thus shortening the chain by one and returning in the safe margin", self.nodes, DEBUG_MODE)
        self.nodes[0].invalidateblock(self.nodes[0].getbestblockhash())
        sync_mempools(self.nodes[1:3])

        mark_logs("Node0 sending raw certificate for epoch {}, expecting success".format(epoch_number), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[0].sendrawtransaction(signed_cert['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)
            assert_true(False)

        decoded_cert_pre = self.nodes[0].decoderawtransaction(signed_cert['hex'])
        decoded_cert_pre_list = sorted(decoded_cert_pre.items())

        sync_mempools(self.nodes[1:3])

        mark_logs("Node0 generating 4 block, also reverting other nodes' chains", self.nodes, DEBUG_MODE)
        mined = self.nodes[0].generate(1)[0]
        minedBlock = self.nodes[0].getblock(mined)
        #epoch_number = 1
        self.nodes[0].generate(3)
        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        self.sync_all()

        # -------------------------- end epoch

        mark_logs("Node2 tries to send to Node1 spending immature backward transfers, expecting failure...", self.nodes, DEBUG_MODE)
        # vout 0 is the change, vout 1 is the bwt
        inputs = [{'txid': cert, 'vout': 1}]
        rawtx_amount = Decimal("3.99995")
        outputs = { self.nodes[1].getnewaddress() : rawtx_amount }
        rawtx=self.nodes[2].createrawtransaction(inputs, outputs)
        sigRawtx = self.nodes[2].signrawtransaction(rawtx)
        try:
            rawtx = self.nodes[2].sendrawtransaction(sigRawtx['hex'])
            assert_true(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("======> ", errorString, "\n")

        sc_funds_post = self.nodes[3].getscinfo(scid)['items'][0]['balance']
        assert_equal(sc_funds_post, sc_funds_pre - bt_amount)

        decoded_cert_post = self.nodes[2].getrawtransaction(cert, 1)
        assert_equal(decoded_cert_post['txid'], cert)
        assert_equal(decoded_cert_post['hex'], signed_cert['hex'])
        assert_equal(decoded_cert_post['blockhash'], mined)
        assert_equal(decoded_cert_post['confirmations'], 4)
        assert_equal(decoded_cert_post['height'], minedBlock['height'])
        assert_equal(decoded_cert_post['blocktime'], minedBlock['time'])
        assert_equal(decoded_cert_post['time'], minedBlock['time'])

        #remove fields not included in decoded_cert_pre_list
        del decoded_cert_post['hex']
        del decoded_cert_post['blockhash']
        del decoded_cert_post['confirmations']
        del decoded_cert_post['blocktime']
        del decoded_cert_post['height']
        del decoded_cert_post['time']
        decoded_cert_post_list = sorted(decoded_cert_post.items())

        mark_logs("check that cert decodes correctly", self.nodes, DEBUG_MODE)
        assert_equal(decoded_cert_pre_list, decoded_cert_post_list)

        mark_logs("check that SC balance has been decreased by the cert amount", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[2].getscinfo(scid)['items'][0]['balance'], (sc_amount - bt_amount))

        node0_bal_before = self.nodes[0].getbalance()

        # create wCert proof
        quality = 1
        proof = mcTest.create_test_proof("sc1",
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant)

        raw_params = {
            "scid": scid,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash,
            "scProof": proof,
            "withdrawalEpochNumber": epoch_number
        }
        raw_cert = []
        cert = []

        utx, change = get_spendable(0, CERT_FEE)

        raw_inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        raw_outs    = { self.nodes[0].getnewaddress() : change }
        raw_bwt_outs = []

        # generate a certificate with no backward transfers and only a fee
        try:
            raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[0].signrawtransaction(raw_cert)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)
            assert_true(False)

        decoded_cert_pre = self.nodes[0].decoderawtransaction(signed_cert['hex'])
        decoded_cert_pre_list = sorted(decoded_cert_pre.items())

        mark_logs("Node3 sending raw certificate with no backward transfer for epoch {}".format(epoch_number), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[3].sendrawtransaction(signed_cert['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)
            assert_true(False)

        self.sync_all()

        mark_logs("Node2 generating 1 block", self.nodes, DEBUG_MODE)
        mined = self.nodes[2].generate(1)[0]
        minedBlock = self.nodes[2].getblock(mined)
        self.sync_all()

        # we enabled -txindex in zend therefore also node 2 can see it
        decoded_cert_post = self.nodes[2].getrawtransaction(cert, 1)

        mark_logs("check that cert contents are as expected", self.nodes, DEBUG_MODE)
        # vout contains just the change 
        assert_equal(len(decoded_cert_post['vout']), 1)
        assert_equal(decoded_cert_post['txid'], cert)
        assert_equal(decoded_cert_post['hex'], signed_cert['hex'])
        assert_equal(decoded_cert_post['blockhash'], mined)
        assert_equal(decoded_cert_post['confirmations'], 1)
        assert_equal(Decimal(decoded_cert_post['cert']['totalAmount']), 0.0)
        assert_equal(decoded_cert_post['height'], minedBlock['height'])
        assert_equal(decoded_cert_post['blocktime'], minedBlock['time'])
        assert_equal(decoded_cert_post['time'], minedBlock['time'])

        #remove fields not included in decoded_cert_pre_list
        del decoded_cert_post['hex']
        del decoded_cert_post['blockhash']
        del decoded_cert_post['confirmations']
        del decoded_cert_post['blocktime']
        del decoded_cert_post['height']
        del decoded_cert_post['time']
        decoded_cert_post_list = sorted(decoded_cert_post.items())

        mark_logs("check that cert decodes correctly", self.nodes, DEBUG_MODE)
        assert_equal(decoded_cert_pre_list, decoded_cert_post_list)

        # check the miner got the cert fee in his coinbase
        coinbase = self.nodes[3].getblock(mined, True)['tx'][0]
        decoded_coinbase = self.nodes[2].getrawtransaction(coinbase, 1)
        miner_quota = decoded_coinbase['vout'][0]['value']
        mark_logs("check that the miner has got the cert fee", self.nodes, DEBUG_MODE)
        assert_equal(miner_quota, Decimal(MINER_REWARD_POST_H200) + CERT_FEE)

        # check that the Node 0 has been charged with the cert fee
        node0_bal_after = self.nodes[0].getbalance()
        mark_logs("check that the Node 0, the creator of the cert, which have been actually sent by Node3, has been charged with the fee", self.nodes, DEBUG_MODE)
        assert_equal(node0_bal_after, node0_bal_before - CERT_FEE)

        mark_logs("Node0 generating 4 block reaching next epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(4)
        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        self.sync_all()

        # -------------------------- end epoch

        raw_inputs   = []
        raw_outs     = {}
        raw_bwt_outs = []
        raw_cert = []
        cert = []

        # get some UTXO for handling many vin 
        totalAmount = Decimal("100.0")
        totalUtxoAmount = Decimal("0")
        listunspent = self.nodes[3].listunspent()
        for aUtx in listunspent:
            if totalUtxoAmount < totalAmount :
                utx = aUtx
                raw_inputs.append({'txid' : utx['txid'], 'vout' : utx['vout']})
                totalUtxoAmount += utx['amount']
            else:
                break

        assert_true(totalUtxoAmount >= totalAmount)

        change = totalUtxoAmount - CERT_FEE

        numbOfChunks = 50
        chunkValueBt  = Decimal(sc_funds_post/numbOfChunks) 
        chunkValueOut = Decimal(change/numbOfChunks)

        for k in range(0, numbOfChunks):
            addr_node1 = self.nodes[1].getnewaddress()
            raw_bwt_outs.append({"address":addr_node1, "amount":chunkValueBt})
            taddr = self.nodes[3].getnewaddress()
            raw_outs.update({taddr: chunkValueOut})

        totBwtOuts = len(raw_bwt_outs)*chunkValueBt
        totOuts    = len(raw_outs)*chunkValueOut
        certFee    = totalUtxoAmount - Decimal(totOuts)

        # create wCert proof
        quality = 2

        '''
            we need to put addresses and amounts in the corresponding lists in the same order
            as the raw_bwt_outs list, otherwise the bwt merkle root computed in the SNARK prover
            and the verifier won't match
        '''
        addresses = []
        amounts = []
        for entry in raw_bwt_outs: 
            addresses.append(entry["address"])
            amounts.append(entry["amount"])

        proof = mcTest.create_test_proof("sc1",
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant,
                                         pks      = addresses,
                                         amounts  = amounts)

        raw_params = {
            "scid": scid,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash,
            "scProof": proof,
            "withdrawalEpochNumber": epoch_number
        }

        # generate a certificate with some backward transfer, several vin vout and a fee
        try:
            raw_cert    = self.nodes[3].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[3].signrawtransaction(raw_cert)
            # let a different node, Node0, send it
            mark_logs("Node1 sending raw certificate for epoch {}".format(epoch_number), self.nodes, DEBUG_MODE)
            cert        = self.nodes[1].sendrawtransaction(signed_cert['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)
            assert_true(False)

        self.sync_all()
        decoded_cert_post = self.nodes[0].getrawtransaction(cert, 1)

        mark_logs("check that cert contents are as expected", self.nodes, DEBUG_MODE)
        assert_equal(decoded_cert_post['txid'], cert)
        # vin contains the expected numb of utxo
        assert_equal(len(decoded_cert_post['vin']), len(raw_inputs))
        # vout contains the change and the backward transfers 
        assert_equal(len(decoded_cert_post['vout']),  len(raw_outs) + len(raw_bwt_outs))
        assert_equal(decoded_cert_post['hex'], signed_cert['hex'])
        assert_equal(Decimal(decoded_cert_post['cert']['totalAmount']), Decimal(totBwtOuts))
        assert_equal(self.nodes[3].gettransaction(cert)['fee'], -certFee)

        mark_logs("Node0 generating 5 block reaching next epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        self.sync_all()
        
        '''
        # get a UTXO for setting fee
        utx = False
        listunspent = self.nodes[0].listunspent()
        for aUtx in listunspent:
            if aUtx['amount'] > CERT_FEE:
                utx = aUtx
                change = aUtx['amount'] - CERT_FEE
                break

        assert_equal(utx!=False, True)
        '''
        utx, change = get_spendable(0, CERT_FEE)

        # create wCert proof
        quality = 3

        proof = mcTest.create_test_proof("sc1",
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant)

        raw_inputs   = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        raw_outs     = { self.nodes[0].getnewaddress() : change }
        raw_bwt_outs = []
        raw_params = {
            "scid": scid,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash,
            "scProof": proof,
            "withdrawalEpochNumber": epoch_number
        }
        raw_cert     = []
        pk_arr       = []

        # generate a certificate which is expected to fail to be signed by passing a wrong private key
        pk_arr = []
        pk_bad = self.nodes[1].dumpprivkey(self.nodes[1].getnewaddress() )
        pk_arr.append(pk_bad)

        try:
            mark_logs("Node0 creates and signs a raw certificate for epoch {}, expecting failure because the priv key is not his...".format(epoch_number), self.nodes, DEBUG_MODE)
            raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[0].signrawtransaction(raw_cert, [], pk_arr)
            assert_equal(signed_cert['complete'], False)
            print("======> ", signed_cert['errors'][0]['error'], "\n")
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)
            assert_true(False)

        # retry adding the right key
        pk_good = self.nodes[0].dumpprivkey(utx['address'])
        pk_arr.append(pk_good)

        try:
            mark_logs("Node0 creates and signs a raw certificate for epoch {}, expecting success because the priv key is the right one...".format(epoch_number), self.nodes, DEBUG_MODE)
            raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[0].signrawtransaction(raw_cert, [], pk_arr)
            assert_equal(signed_cert['complete'], True)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)

        mark_logs("Node2 sending raw certificate for epoch {}".format(epoch_number), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[2].sendrawtransaction(signed_cert['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)
            assert_true(False)

        self.sync_all()

        mark_logs("Node2 retries to send the same failed tx to Node1 spending now matured backward transfers", self.nodes, DEBUG_MODE)
        try:
            rawtx = self.nodes[2].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)
            assert_true(False)

        self.sync_all()

        mark_logs("Check tx is in mempool", self.nodes, DEBUG_MODE)
        assert_equal(True, rawtx in self.nodes[2].getrawmempool())

        bal_1_pre = self.nodes[1].getbalance()

        mark_logs("Node 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        bal_1_post = self.nodes[1].getbalance()
        mark_logs("Verify Node 1 balance", self.nodes, DEBUG_MODE)
        assert_equal(bal_1_post - bal_1_pre, rawtx_amount)

        mark_logs("Node 0 tries to send a certificate for old epoch {}, expecting failure...".format(epn_0), self.nodes, DEBUG_MODE)
        utx, change = get_spendable(0, CERT_FEE)
        raw_inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        raw_outs    = { self.nodes[0].getnewaddress() : change }
        raw_bwt_outs = []

        raw_params = {
            "scid": scid,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash_0,
            "scProof": proof,
            "withdrawalEpochNumber": epn_0
        }
        try:
            raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[0].signrawtransaction(raw_cert)
            cert = self.nodes[0].sendrawtransaction(signed_cert['hex'])
            assert_true(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("======> ", errorString, "\n")

        mark_logs("Node0 generating 4 block reaching next epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(4)
        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        self.sync_all()
        
        # create wCert proof
        quality = 1

        proof = mcTest.create_test_proof("sc1",
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant)

        raw_params = {
            "scid": scid,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash,
            "scProof": proof,
            "withdrawalEpochNumber": epoch_number
        }
        raw_cert = []
        cert = []

        raw_inputs  = []
        raw_outs    = {}
        raw_bwt_outs = []

        # generate a certificate with no backward transfers, no vin and no change nor fee
        try:
            raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[0].signrawtransaction(raw_cert)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)
            assert_true(False)

        decoded_cert_pre = self.nodes[0].decoderawtransaction(signed_cert['hex'])
        decoded_cert_pre_list = sorted(decoded_cert_pre.items())

        mark_logs("Node3 sending raw certificate with no vin for epoch {}, expecting failure...".format(epoch_number), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[3].sendrawtransaction(signed_cert['hex'])
            assert_true(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("======> ", errorString, "\n")

        self.sync_all()

        # generate a certificate with no fee (vin = vout)
        utx, change = get_spendable(0, Decimal("0.0"))
        raw_inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        raw_outs    = { self.nodes[0].getnewaddress() : change }

        try:
            raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[0].signrawtransaction(raw_cert)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)
            assert_true(False)

        mark_logs("Node3 sending raw certificate with no fee for epoch {}...".format(epoch_number), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[3].sendrawtransaction(signed_cert['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)
            assert_true(False)

        self.sync_all()

        mark_logs("Node2 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[2].generate(1)
        self.sync_all()

        # check it is in blockchain and has really a 0 fee
        for x in self.nodes[0].listtransactions():
            if x['txid'] == cert and x['category'] == "send":
                certFee = x['fee']
                conf = x['confirmations']
                break

        assert_equal(conf, 1)
        assert_equal(certFee, Decimal("0.0"))


        # generate a certificate with invalid FT fee
        errorString = ""
        ftScFee = Decimal("-1.0")
        raw_params = {
            "scid": scid,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash,
            "scProof": proof,
            "withdrawalEpochNumber": epoch_number,
            "ftScFee": ftScFee
        }

        mark_logs("Node0 creating raw certificate with negative FT fee", self.nodes, DEBUG_MODE)
        try:
            raw_cert = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            assert_true(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)

        assert_true("Amount out of range" in errorString)


        # generate a certificate with invalid MBTR fee
        errorString = ""
        mbtrScFee = Decimal("-1.0")
        raw_params = {
            "scid": scid,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash,
            "scProof": proof,
            "withdrawalEpochNumber": epoch_number,
            "mbtrScFee": mbtrScFee
        }

        mark_logs("Node0 creating raw certificate with negative MBTR fee", self.nodes, DEBUG_MODE)
        try:
            raw_cert = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            assert_true(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)

        assert_true("Amount out of range" in errorString)


        # generate a certificate with valid FT and MBTR fees
        errorString = ""
        ftScFee = Decimal("10.0")
        mbtrScFee = Decimal("20.0")
        raw_params = {
            "scid": scid,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash,
            "scProof": proof,
            "withdrawalEpochNumber": epoch_number,
            "ftScFee": ftScFee,
            "mbtrScFee": mbtrScFee
        }

        mark_logs("Node0 creating raw certificate with valid FT and MBTR fees", self.nodes, DEBUG_MODE)
        try:
            raw_cert = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            decoded_cert = self.nodes[0].decoderawtransaction(raw_cert)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)
            assert_true(False)

        assert_equal(decoded_cert['cert']['ftScFee'], ftScFee)
        assert_equal(decoded_cert['cert']['mbtrScFee'], mbtrScFee)

if __name__ == '__main__':
    sc_rawcert().main()
