#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test checkcswnullifier for presence nullifer in MC.
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, connect_nodes_bi, assert_true, assert_false, mark_logs, \
    wait_bitcoinds, stop_nodes, get_epoch_data, sync_mempools, sync_blocks, \
    disconnect_nodes, advance_epoch, swap_bytes
from test_framework.test_framework import MINIMAL_SC_HEIGHT, MINER_REWARD_POST_H200
from test_framework.mc_test.mc_test import CertTestUtils, CSWTestUtils, generate_random_field_element_hex

from decimal import Decimal
import pprint
import time

NUMB_OF_NODES = 3
DEBUG_MODE = 1
EPOCH_LENGTH = 6
CERT_FEE = Decimal('0.0001')


# Create one-input, one-output, no-fee transaction:
class CswNullifierTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[["-sccoinsmaturity=0", '-scproofqueuesize=0', '-logtimemicros=1', '-debug=sc', '-debug=py',
                                              '-debug=mempool', '-debug=net', '-debug=bench']] * NUMB_OF_NODES)

        if not split:
            # 1 and 2 are joint only if split==false
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:3])
            sync_mempools(self.nodes[1:3])

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def split_network(self):
        # Split the network of three nodes into nodes 0-1 and 2.
        assert not self.is_network_split
        disconnect_nodes(self.nodes[1], 2)
        disconnect_nodes(self.nodes[2], 1)
        self.is_network_split = True

    def join_network(self):
        # Join the (previously split) network pieces together: 0-1-2
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 1)
        time.sleep(2)
        self.is_network_split = False

    def run_test(self):
        '''
        Create a SC, advance two epochs and then let it cease.
        Create two transactions, each one containing a CSW with
        an input value covering the whole sidechain balance.
        Even though the first CSW transaction is still in mempool,
        the second one should be rejected.
        '''

        # prepare some coins 
        self.nodes[0].generate(MINIMAL_SC_HEIGHT)
        self.sync_all()

        sc_address = "0000000000000000000000000000000000000000000000000000000000000abc"
        sc_epoch_len = EPOCH_LENGTH
        sc_cr_amount = Decimal('12.00000000')

        certMcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        cswMcTest = CSWTestUtils(self.options.tmpdir, self.options.srcdir)

        # generate wCertVk and constant
        vk = certMcTest.generate_params("sc1")
        cswVk = cswMcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()

        sc_cr = []
        sc_cr.append({
            "version": 0,
            "epoch_length": sc_epoch_len,
            "amount": sc_cr_amount,
            "address": sc_address,
            "wCertVk": vk,
            "wCeasedVk": cswVk,
            "constant": constant
        })

        rawtx = self.nodes[0].createrawtransaction([], {}, [], sc_cr)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        self.sync_all()

        decoded_tx = self.nodes[2].getrawtransaction(finalRawtx, 1)
        scid = decoded_tx['vsc_ccout'][0]['scid']
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)
        print

        # advance two epochs
        mark_logs("\nLet 2 epochs pass by...", self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
             scid, "sc1", constant, sc_epoch_len)

        mark_logs("\n==> certificate for epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)


        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
             scid, "sc1", constant, sc_epoch_len)

        mark_logs("\n==> certificate for epoch {} {}l".format(epoch_number, cert), self.nodes, DEBUG_MODE)


        # mine one block for having last cert in chain
        mark_logs("\nNode0 generates 1 block confirming last cert", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Let SC cease... ", self.nodes, DEBUG_MODE)

        nbl = int(sc_epoch_len * 1.5)
        mark_logs("Node0 generates {} blocks".format(nbl), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(nbl)
        self.sync_all()

        # check it is really ceased
        ret = self.nodes[0].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")

        # and has the expected balance
        sc_bal = self.nodes[0].getscinfo(scid, False, False)['items'][0]['balance']
        assert_equal(sc_bal, sc_cr_amount)

        mark_logs("\nCreate a CSW withdrawing 90% of the sc balance... ", self.nodes, DEBUG_MODE)

        # CSW sender MC address
        csw_mc_address = self.nodes[0].getnewaddress()

        sc_csw_amount = sc_bal * Decimal("0.9")
        null1 = generate_random_field_element_hex()
        actCertData = self.nodes[0].getactivecertdatahash(scid)['certDataHash']
        print "Active Cert Data Hash: -------> ", actCertData

        ceasingCumScTxCommTree = self.nodes[0].getceasingcumsccommtreehash(scid)['ceasingCumScTxCommTree']

        scid_swapped = swap_bytes(scid)
        sc_proof1 = cswMcTest.create_test_proof(
                "sc1", sc_csw_amount, str(scid_swapped), null1, csw_mc_address, ceasingCumScTxCommTree, actCertData, constant)

        sc_csws = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": null1,
            "activeCertData": actCertData,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
            "scProof": sc_proof1
        }]

        # recipient MC address
        taddr_2 = self.nodes[2].getnewaddress()
        sc_csw_tx_outs = {taddr_2: sc_csw_amount}

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        mark_logs("sent csw 1 {} retrieving {} coins on Node2 behalf".format(finalRawtx, sc_csws[0]['amount']), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Check csw is in mempool...", self.nodes, DEBUG_MODE)
        assert_true(finalRawtx in self.nodes[2].getrawmempool())

        mark_logs("\nCreate a second CSW withdrawing 90% of the sc balance (should be rejected)... ", self.nodes, DEBUG_MODE)

        # CSW sender MC address
        csw_mc_address = self.nodes[0].getnewaddress()

        sc_csw_amount = sc_bal * Decimal("0.9")
        null1 = generate_random_field_element_hex()

        sc_proof1 = cswMcTest.create_test_proof(
                "sc1", sc_csw_amount, str(scid_swapped), null1, csw_mc_address, ceasingCumScTxCommTree, actCertData, constant)

        sc_csws = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": null1,
            "activeCertData": actCertData,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
            "scProof": sc_proof1
        }]

        # recipient MC address
        taddr_2 = self.nodes[2].getnewaddress()
        sc_csw_tx_outs = {taddr_2: sc_csw_amount}

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")

        try:
            finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
            assert(False)
        except JSONRPCException, e:
            error_string = e.error['message']
            mark_logs("Failed sending csw 2 {} retrieving {} coins on Node2 behalf, error message: {}".format(finalRawtx, sc_csws[0]['amount'], error_string), self.nodes, DEBUG_MODE)
            assert_true("bad-sc-tx-not-applicable" in error_string)
        self.sync_all()

if __name__ == '__main__':
    CswNullifierTest().main()
