#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs,\
    get_epoch_data, swap_bytes, \
    assert_false, assert_true

from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200

from test_framework.mc_test.mc_test import *
import os
import pprint
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 3
EPOCH_LENGTH = 5
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')


class sc_cert_getraw(BitcoinTestFramework):

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
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib', '-scproofqueuesize=0', '-logtimemicros=1'],
             ['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib', '-scproofqueuesize=0', '-logtimemicros=1'],
             ['-txindex=1', '-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib', '-scproofqueuesize=0', '-logtimemicros=1']
            ])

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        The test creates a sc, send funds to it and then sends a certificate to it,
        Then test that getrawtransaction decodes correctly tx and cert when possible. 
        '''

        # forward transfer amounts
        creation_amount = Decimal("0.5")
        fwt_amount = Decimal("50")
        bwt_amount = Decimal("50")

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()

        # SC creation
        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk_tag = "sc1"
        vk = mcTest.generate_params(vk_tag)
        constant = generate_random_field_element_hex()
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": constant,
        }

        ret = self.nodes[1].sc_create(cmdInput)

        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs("Node 1 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        decoded_tx_mempool = self.nodes[1].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx_mempool['vsc_ccout'][0]['scid'])

        decoded_tx_mempool_hex = self.nodes[1].getrawtransaction(creating_tx)
        dec = self.nodes[1].decoderawtransaction(decoded_tx_mempool_hex)
        assert_equal(creating_tx, dec['txid'])
        assert_equal(scid, dec['vsc_ccout'][0]['scid'])

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        sc_creating_height = self.nodes[0].getblockcount()
        self.sync_all()

        decoded_tx_notxindex = self.nodes[1].getrawtransaction(creating_tx, 1)
        assert_equal(decoded_tx_mempool['hex'], decoded_tx_notxindex['hex'])

        decoded_tx_txindex = self.nodes[2].getrawtransaction(creating_tx, 1)
        assert_equal(decoded_tx_mempool['hex'], decoded_tx_txindex['hex'])

        # Fwd Transfer to Sc
        mark_logs("Node0 sends fwd transfer", self.nodes, DEBUG_MODE)

        mc_return_address = self.nodes[0].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount, "scid": scid, 'mcReturnAddress': mc_return_address}]
        fwd_tx = self.nodes[0].sc_send(cmdInput)

        self.sync_all()

        decoded_tx_mempool = self.nodes[1].getrawtransaction(fwd_tx, 1)
        assert_equal(scid, decoded_tx_mempool['vft_ccout'][0]['scid'])

        mark_logs("Node0 confirms fwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        decoded_tx_notxindex = self.nodes[1].getrawtransaction(fwd_tx, 1)
        assert_equal(decoded_tx_mempool['hex'], decoded_tx_notxindex['hex'])
        decoded_tx_txindex = self.nodes[2].getrawtransaction(fwd_tx, 1)
        assert_equal(decoded_tx_mempool['hex'], decoded_tx_txindex['hex'])

        mark_logs("Node0 generating 3 more blocks to achieve end of withdrawal epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(3)
        self.sync_all()

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        addr_node1 = self.nodes[1].getnewaddress()
        amount_cert_1 = [{"address": addr_node1, "amount": bwt_amount}]

        #Create proof for WCert
        quality = 0
        proof = mcTest.create_test_proof(
            vk_tag, scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node1], [bwt_amount])

        mark_logs("Node 0 performs a bwd transfer of {} coins to Node1 address {}".format(amount_cert_1[0]["address"], amount_cert_1[0]["amount"]), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_0 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("Certificate is {}".format(cert_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        decoded_cert_mempool = self.nodes[1].getrawtransaction(cert_epoch_0, 1)
        decoded_cert_mempool2 = self.nodes[1].getrawtransaction(cert_epoch_0, 1)
        assert_equal(decoded_cert_mempool, decoded_cert_mempool2)
        assert_equal(scid, decoded_cert_mempool['cert']['scid'])

        decoded_cert_mempool_hex = self.nodes[1].getrawtransaction(cert_epoch_0)
        decoded_cert_mempool_hex2 = self.nodes[1].getrawtransaction(cert_epoch_0)
        assert_equal(decoded_cert_mempool_hex, decoded_cert_mempool_hex2)
        dec = self.nodes[2].decoderawtransaction(decoded_cert_mempool_hex)
        assert_equal(cert_epoch_0, dec['txid'])
        assert_equal(scid, dec['cert']['scid'])
        dec2 = self.nodes[2].decoderawtransaction(decoded_cert_mempool_hex)
        assert_equal(dec2, dec)

        mark_logs("Node0 confims bwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        mined = self.nodes[0].generate(1)[0]
        self.sync_all()

        decoded_cert_notxindex = self.nodes[1].getrawtransaction(cert_epoch_0, 1)
        assert_equal(decoded_cert_mempool['hex'], decoded_cert_notxindex['hex'])
        decoded_cert_txindex = self.nodes[2].getrawtransaction(cert_epoch_0, 1)
        assert_equal(decoded_cert_mempool['hex'], decoded_cert_txindex['hex'])

        mark_logs("Node0 generating enough blocks to move to new withdrawal epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(EPOCH_LENGTH - 1)
        self.sync_all()

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        mark_logs("Generate new certificate for epoch {}. No bwt and no fee are included".format(epoch_number), self.nodes, DEBUG_MODE)

        # Create new proof for WCert
        quality = 1
        proof = mcTest.create_test_proof(vk_tag, scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [], [])

        nullFee = Decimal("0.0")
        try:
            cert_epoch_1 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, [], FT_SC_FEE, MBTR_SC_FEE, nullFee)
            mark_logs("Certificate is {}".format(cert_epoch_1), self.nodes, DEBUG_MODE)
            self.sync_all()
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Check the certificate for this scid has no vin and no vouts", self.nodes, DEBUG_MODE)
        decoded_cert_mempool = self.nodes[0].getrawtransaction(cert_epoch_1, 1)
        assert_equal(decoded_cert_mempool['cert']['scid'], scid)
    
        mark_logs("Node0 confims bwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # no more in mempool, only node with txindex=1 can decode it
        try:
            decoded_cert_notxindex = self.nodes[1].getrawtransaction(cert_epoch_1, 1)
        except JSONRPCException as e:
            errorString = e.error['message']
            assert_equal("No information" in errorString, True)

        decoded_cert_txindex = self.nodes[2].getrawtransaction(cert_epoch_1, 1)
        assert_equal(decoded_cert_mempool['hex'], decoded_cert_txindex['hex'])


if __name__ == '__main__':
    sc_cert_getraw().main()
