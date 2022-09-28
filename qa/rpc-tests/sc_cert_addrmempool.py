#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import MINIMAL_SC_HEIGHT, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    sync_blocks, sync_mempools, wait_bitcoinds, mark_logs, \
    assert_false, assert_true, swap_bytes, wait_and_assert_operationid_status, \
    start_nodes, start_node, connect_nodes, stop_nodes, stop_node, get_epoch_data, \
    to_satoshis, get_spendable

from test_framework.mc_test.mc_test import *

import os
import shutil
from random import randint
from decimal import Decimal
import logging
import pprint
import time

DEBUG_MODE = 1
NUMB_OF_NODES = 4
EPOCH_LENGTH = 20
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')

logging.basicConfig(format='%(levelname)s:%(message)s', level=logging.INFO)

class AddresMempool(BitcoinTestFramework):

    def setup_chain(self):
        logging.info("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    # This mirrors how the network was setup in the bash test
    def setup_network(self, split=False):
        self.nodes = []

        extra_args = [
           ['-debug=py', '-debug=sc', '-debug=mempool', '-debug=cert', '-scproofqueuesize=0', '-logtimemicros=1'],
           ['-debug=py', '-debug=sc', '-debug=mempool', '-debug=cert', '-scproofqueuesize=0', '-logtimemicros=1'],
           ['-debug=py', '-debug=sc', '-debug=mempool', '-debug=cert', '-scproofqueuesize=0', '-logtimemicros=1'],
           ['-txindex', '-addressindex', '-timestampindex', '-spentindex',
           '-debug=py', '-debug=sc', '-debug=mempool', '-debug=cert', '-scproofqueuesize=0', '-logtimemicros=1']
           ]

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args)

        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[1], 2)
        connect_nodes(self.nodes[2], 3)
        connect_nodes(self.nodes[3], 0)
        self.is_network_split=False
        self.sync_all()

    def run_test(self):

        '''
        Test the accept and removal of certificates in the mempool, checking that the quality is correctly handled
        and reported as expected in the output of the rpc cmd getaddressmempool().
        This test can be run only if the -addressindex', '-timestampindex', '-spentindex' options are supported by the zend_oo binary
        '''

        logging.info("Generating initial blockchain")
        self.nodes[0].generate(100)
        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()

        # reach sidechain fork
        nb = int(self.nodes[0].getblockcount())
        node2_cb_height = 341

        nb_to_gen1 = node2_cb_height - nb
        if nb_to_gen1 > 0:
            mark_logs("Node 0 generates {} block".format(nb_to_gen1), self.nodes, DEBUG_MODE)
            self.nodes[0].generate(nb_to_gen1)
            self.sync_all()

        mark_logs("Node 2 generates 1 block for coinbase to be used after 100 blocks", self.nodes, DEBUG_MODE)
        self.nodes[2].generate(1)
        self.sync_all()
        print("Chain height=", self.nodes[3].getblockcount())

        # reach sidechain fork
        nb = int(self.nodes[0].getblockcount())
        nb_to_gen = MINIMAL_SC_HEIGHT - nb -1
        if nb_to_gen > 0:
            mark_logs("Node 0 generates {} block for reaching sc fork".format(nb_to_gen), self.nodes, DEBUG_MODE)
            self.nodes[0].generate(nb_to_gen)
            self.sync_all()

        print("Chain height=", self.nodes[3].getblockcount())

        safe_guard_size = EPOCH_LENGTH//5
        if safe_guard_size < 2:
            safe_guard_size = 2

        creation_amount = Decimal("1.0")

        prev_epoch_hash = self.nodes[0].getbestblockhash()

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()

        # Create a SC
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': EPOCH_LENGTH,
            'toaddress': "dada",
            'amount': creation_amount,
            'wCertVk': vk,
            'constant': constant
        }

        ret = self.nodes[0].sc_create(cmdInput)
        scid = ret['scid']
        tx_cr = ret['txid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs("tx={} created SC id: {}".format(tx_cr, scid), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        sc_creating_height = self.nodes[0].getblockcount()
        self.sync_all()

        mark_logs("Node0 generates {} more blocks to achieve end of withdrawal epochs".format(EPOCH_LENGTH - 1), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(EPOCH_LENGTH - 1)
        self.sync_all()

        mark_logs("Node0 generates 3 more blocks to give spendable funds to node2", self.nodes, DEBUG_MODE)
        bl = self.nodes[0].generate(3)
        #self.nodes[0].generate(1)
        self.sync_all()

        utxos_2 = self.nodes[2].listunspent()
        #pprint.pprint(utxos_2)
        assert_equal(len(utxos_2), 1)
        assert_equal(utxos_2[0]['confirmations'], 101)

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        taddr0 = self.nodes[0].getnewaddress()
        taddr1 = self.nodes[1].getnewaddress()
        taddr2 = self.nodes[2].getnewaddress()

        node0Addr = self.nodes[0].validateaddress(taddr0)['address']
        node1Addr = self.nodes[1].validateaddress(taddr1)['address']
        node2Addr = self.nodes[2].validateaddress(taddr2)['address']

        bwt_amount0      = Decimal("0.10")
        bwt_amount1      = Decimal("0.20")
        bwt_amount2      = Decimal("0.30")

        try:
            #Create proof for WCert
            quality = 1
            amounts = [{"address": node0Addr, "amount": bwt_amount0}]
            proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality,
                MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node0Addr], [bwt_amount0])

            mark_logs("Node 0 sends a cert with a bwd transfers of {} coins to Node0 taddr {}".format(bwt_amount0, taddr0), self.nodes, DEBUG_MODE)
            cert_0_top = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("==> certificate is {}".format(cert_0_top), self.nodes, DEBUG_MODE)
            self.sync_all()

            quality = 2
            amounts = [{"address": node1Addr, "amount": bwt_amount1}]
            proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality,
                MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount1])

            mark_logs("Node 1 sends a cert with a bwd transfers of {} coins to Node1 taddr {}".format(bwt_amount1, taddr1), self.nodes, DEBUG_MODE)
            cert_1_top = self.nodes[1].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("==> certificate is {}".format(cert_1_top), self.nodes, DEBUG_MODE)

            self.sync_all()

            quality = 3
            amounts = [{"address": node2Addr, "amount": bwt_amount2}]
            proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality,
                MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node2Addr], [bwt_amount2])

            mark_logs("Node 2 sends a cert with a bwd transfers of {} coins to Node2 taddr {}".format(bwt_amount2, taddr2), self.nodes, DEBUG_MODE)
            cert_2_top = self.nodes[2].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("==> certificate is {}".format(cert_2_top), self.nodes, DEBUG_MODE)

            self.sync_all()

            cert_hex = self.nodes[2].getrawtransaction(cert_2_top)

        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        print("Calling getcertmaturityinfo for cert {} , it should be in mempool, non top quality".format(cert_0_top))
        ret = self.nodes[0].getcertmaturityinfo(cert_0_top)
        assert_equal(ret['blocksToMaturity'], -1)
        assert_equal(ret['certificateState'], "LOW_QUALITY_MEMPOOL")
        assert_equal(ret['maturityHeight'], -1)

        print("Calling getcertmaturityinfo for cert {} , it should be in mempool, top quality".format(cert_2_top))
        ret = self.nodes[0].getcertmaturityinfo(cert_2_top)
        assert_equal(ret['blocksToMaturity'], -1)
        assert_equal(ret['certificateState'], "TOP_QUALITY_MEMPOOL")
        assert_equal(ret['maturityHeight'], -1)

        addr_list = []
        addr_list.append(taddr0)
        addr_list.append(taddr1)
        addr_list.append(taddr2)

        args = {"addresses": addr_list} 
        ret = self.nodes[3].getaddressmempool(args)
        #pprint.pprint(ret)

        for x in ret:
            addr = x['address']
            sat = x['satoshis']
            if sat < 0:
                # skip inputs
                continue

            cert_id = x['txid']
            out_status = x['outstatus']
            if addr == taddr0:
                assert_equal(sat, to_satoshis(bwt_amount0)) 
                assert_equal(cert_id, cert_0_top)
                assert_equal(out_status, 2)
            if addr == taddr1:
                assert_equal(sat, to_satoshis(bwt_amount1)) 
                assert_equal(cert_id, cert_1_top)
                assert_equal(out_status, 2)
            if addr == taddr2:
                assert_equal(sat, to_satoshis(bwt_amount2)) 
                assert_equal(cert_id, cert_2_top)
                assert_equal(out_status, 1)

        # start first epoch + 2*epocs + safe guard
        bwtMaturityHeight = (sc_creating_height-1) + 2*EPOCH_LENGTH + safe_guard_size

        # revert last 3 blocks, thus causing the eviction of top quality cert from mempool
        # since its input, which is a coinbase, is not mature anymore
        mark_logs("Nodes revert last 3 blocks", self.nodes, DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            self.nodes[i].invalidateblock(bl[0])
        self.sync_all()

        # cert_2_top is not in mempool, has been removed
        for i in range(0, NUMB_OF_NODES):
            assert_equal(False, cert_2_top in self.nodes[i].getrawmempool())

        ret = self.nodes[3].getaddressmempool(args)

        for x in ret:
            addr = x['address']

            # last top quality cert has been removed from mempool address data too
            assert_false(addr == taddr2)

            sat = x['satoshis']
            if sat < 0:
                # skip inputs
                continue

            cert_id = x['txid']
            out_status = x['outstatus']
            if addr == taddr0:
                assert_equal(sat, to_satoshis(bwt_amount0)) 
                assert_equal(cert_id, cert_0_top)
                assert_equal(out_status, 2)
            if addr == taddr1:
                assert_equal(sat, to_satoshis(bwt_amount1)) 
                assert_equal(cert_id, cert_1_top)
                # last superseeded quality cert has been promoted to top
                assert_equal(out_status, 1)


        mark_logs("Node0 generates 3 more block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(3)
        self.sync_all()

        # coinbase utxo in node2 wallet is spendable again
        utxos_2 = self.nodes[2].listunspent()
        assert_equal(len(utxos_2), 1)
        assert_equal(utxos_2[0]['confirmations'], 101)

        mark_logs("Node2 resend a certificate", self.nodes, DEBUG_MODE)
        try:
            # slightly increase the fee, just for not having the same cert id hash. The reason is that an inventory already
            # known (and this would be since already broadcasted and then evicted) are not broadcasted by th p2p network
            new_cert_fee = CERT_FEE + Decimal(0.00001)
            cert_2_top_retried = self.nodes[2].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, new_cert_fee)

            mark_logs("==> certificate is {}".format(cert_2_top_retried), self.nodes, DEBUG_MODE)
            assert_true(cert_2_top_retried != cert_2_top)

            self.sync_all()

        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        assert_true(cert_2_top_retried in self.nodes[0].getrawmempool())

        # the certificate in blockchain is still the top quality
        print("Calling getcertmaturityinfo for cert {}".format(cert_1_top))
        ret = self.nodes[3].getcertmaturityinfo(cert_1_top)
        #pprint.pprint(ret)

        nb = self.nodes[3].getblockcount()
        bl_to_mat = bwtMaturityHeight - nb 
        print("nb = {}, bwtMaturityHeight = {}".format(nb, bwtMaturityHeight))
        assert_equal(ret['blocksToMaturity'], bl_to_mat)
        assert_equal(ret['certificateState'], "IMMATURE")
        assert_equal(ret['maturityHeight'], bwtMaturityHeight)

        mark_logs("Calling getcertmaturityinfo for cert {} ...".format(cert_2_top_retried), self.nodes, DEBUG_MODE)
        try:
            ret = self.nodes[3].getcertmaturityinfo(cert_2_top_retried)
            assert_equal(ret['blocksToMaturity'], -1)
            assert_equal(ret['certificateState'], "TOP_QUALITY_MEMPOOL")
            assert_equal(ret['maturityHeight'], -1)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        mark_logs("Calling getaddressmempool for cert {}".format(cert_2_top_retried), self.nodes, DEBUG_MODE)
        ret = self.nodes[3].getaddressmempool(args)
        #pprint.pprint(ret)

        for x in ret:
            sat = x['satoshis']
            if sat < 0:
                # skip inputs
                continue

            addr       = x['address']
            cert_id    = x['txid']
            out_status = x['outstatus']

            assert_equal(addr, taddr2)
            assert_equal(sat, to_satoshis(bwt_amount2)) 
            assert_equal(cert_id, cert_2_top_retried)
            assert_equal(out_status, 1)

        
        # create wCert proof
        mark_logs("Node 0 sends a cert with 2 outs and 2 bwts", self.nodes, DEBUG_MODE)
        quality = 10
        am_bwt1 = Decimal(0.011)
        am_bwt2 = Decimal(0.022)
        am_out  = Decimal(0.001)

        # python orders dictionaries by key, therefore we must use the same order when creating the proof
        pkh_arr = []
        am_bwt_arr = []
        raw_bwt_outs = [
            {"address": node1Addr, "amount": am_bwt1},
            {"address": node2Addr, "amount": am_bwt2}
        ]
        for entry in raw_bwt_outs:
            pkh_arr.append(entry["address"])
            am_bwt_arr.append(entry["amount"])
 
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant,
            pkh_arr, am_bwt_arr)
        
        utx, change = get_spendable(self.nodes[0], CERT_FEE + am_out)
        raw_inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]

        # do not use the same address, this is not supported (and python would prevent it anyway since this is a dictionary)
        taddr3 = self.nodes[3].getnewaddress()
        raw_outs    = { taddr0: change, taddr3: am_out }

        raw_params = {
            "scid": scid,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash,
            "scProof": proof,
            "withdrawalEpochNumber": epoch_number
        }
        raw_cert = []
        cert_last = []

        try:
            raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[0].signrawtransaction(raw_cert)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("\n======> ", errorString)
            assert_true(False)

        #pprint.pprint(self.nodes[0].decoderawcertificate(signed_cert['hex']))

        try:
            cert_last = self.nodes[0].sendrawtransaction(signed_cert['hex'])
            self.sync_all()
        except JSONRPCException as e:
            errorString = e.error['message']
            print("======> ", errorString, "\n")
            assert_true(False)

        addr_list.append(taddr3)
        args = {"addresses": addr_list} 
        mark_logs("Calling getaddressmempool for cert {}".format(cert_last), self.nodes, DEBUG_MODE)
        ret = self.nodes[3].getaddressmempool(args)
        #pprint.pprint(ret)

        for x in ret:
            sat = x['satoshis']
            if sat < 0:
                # skip inputs
                continue

            addr = x['address']
            cert_id = x['txid']
            out_status = x['outstatus']

            # ordinary outputs
            if addr == taddr0:
                assert_equal(sat, to_satoshis(change)) 
                assert_equal(out_status, 0)
            if addr == taddr3:
                assert_equal(sat, to_satoshis(am_out)) 
                assert_equal(out_status, 0)

            # certificates
            if addr == taddr2:
                # two certificates refer to this address, the latest is top quality, the former has been superseeded 
                if cert_id == cert_last:
                    assert_equal(sat, to_satoshis(am_bwt2)) 
                    assert_equal(out_status, 1)
                if cert_id == cert_2_top_retried:
                    assert_equal(sat, to_satoshis(bwt_amount2)) 
                    assert_equal(out_status, 2)
            if addr == taddr1:
                assert_equal(cert_id, cert_last)
                assert_equal(sat, to_satoshis(am_bwt1)) 
                assert_equal(out_status, 1)

        # the certificate in blockchain is still the top quality
        print("Calling getcertmaturityinfo for cert {}".format(cert_1_top))
        ret = self.nodes[3].getcertmaturityinfo(cert_1_top)
        pprint.pprint(ret)

        bl = self.nodes[0].generate(1)
        self.sync_all()

        print("Calling getcertmaturityinfo for cert {}".format(cert_1_top))
        ret = self.nodes[3].getcertmaturityinfo(cert_1_top)
        pprint.pprint(ret)

        print("Calling getcertmaturityinfo for cert {}".format(cert_last))
        ret = self.nodes[3].getcertmaturityinfo(cert_last)
        pprint.pprint(ret)

        mark_logs("\nInvalidating the last block and checking RPC call results...", self.nodes, DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            self.nodes[i].invalidateblock(bl[0])
        self.sync_all()

        print("Calling getcertmaturityinfo for cert {} , it should be in mempool".format(cert_last))
        assert_true(cert_last in self.nodes[3].getrawmempool())
        ret = self.nodes[3].getcertmaturityinfo(cert_last)
        pprint.pprint(ret)
        assert_equal(ret['blocksToMaturity'], -1)
        assert_equal(ret['certificateState'], "TOP_QUALITY_MEMPOOL")
        assert_equal(ret['maturityHeight'], -1)

        print("Clearing the mempool of all nodes...")
        for i in range(0, NUMB_OF_NODES):
            self.nodes[i].clearmempool()
        self.sync_all()

        for i in range(0, NUMB_OF_NODES):
            assert_equal(len(self.nodes[i].getrawmempool()), 0)

        print("Calling getcertmaturityinfo for cert {} , it shouldn't be in mempool anymore".format(cert_last))
        assert_false(cert_last in self.nodes[3].getrawmempool())
        ret = self.nodes[3].getcertmaturityinfo(cert_last)
        pprint.pprint(ret)
        assert_equal(ret['blocksToMaturity'], -1)
        assert_equal(ret['certificateState'], "INVALID")
        assert_equal(ret['maturityHeight'], -1)

        ret = self.nodes[1].verifychain(4, 0)
        assert_equal(ret, True)


if __name__ == '__main__':
    AddresMempool().main()
