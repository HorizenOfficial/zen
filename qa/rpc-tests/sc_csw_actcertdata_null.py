#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, connect_nodes_bi, assert_true, assert_false, mark_logs, \
    wait_bitcoinds, stop_nodes, sync_mempools, sync_blocks, \
    disconnect_nodes, advance_epoch, swap_bytes

from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200

from test_framework.mc_test.mc_test import *

from decimal import Decimal
import pprint
import time
import codecs

NUMB_OF_NODES = 2
DEBUG_MODE = 1
EPOCH_LENGTH = 6
CERT_FEE = Decimal('0.0001')


# Create one-input, one-output, no-fee transaction:
class CswActCertDataTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[["-sccoinsmaturity=0", '-scproofqueuesize=0', '-logtimemicros=1', '-debug=sc', '-debug=py',
                                              '-debug=mempool', '-debug=net', '-debug=bench']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        Create a SC, advance 1 epoch and then let it cease.
        Test that even if act cert data hash is null, a csw can be requested.
        Restart the network and check DB integrity.
        '''

        # prepare some coins 
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()
        prev_epoch_hash = self.nodes[0].getbestblockhash()

        sc_address = "0000000000000000000000000000000000000000000000000000000000000abc"
        sc_epoch_len = EPOCH_LENGTH
        sc_cr_amount = Decimal('12.00000000')

        certMcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        cswMcTest = CSWTestUtils(self.options.tmpdir, self.options.srcdir)

        # generate wCertVk and constant
        certVk = certMcTest.generate_params("sc")
        cswVk = cswMcTest.generate_params("sc")
        constant1 = generate_random_field_element_hex()

        sc_cr = []
        sc_cr.append({
            "version": 0,
            "epoch_length": sc_epoch_len,
            "amount": sc_cr_amount,
            "address": sc_address,
            "wCertVk": certVk,
            "wCeasedVk": cswVk,
            "constant": constant1
        })

        rawtx = self.nodes[0].createrawtransaction([], {}, [], sc_cr)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)
        scid = decoded_tx['vsc_ccout'][0]['scid']
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)
        print

        # advance 1 epoch
        mark_logs("\nLet 1 epochs pass by...", self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
            scid, "sc", constant1, sc_epoch_len)

        mark_logs("\n==> certificate for SC epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        # mine one block for having last cert in chain
        mark_logs("\nNode0 generates 1 block confirming last cert", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Let the SC cease... ".format(scid), self.nodes, DEBUG_MODE)

        nbl = int(sc_epoch_len * 1.5)
        mark_logs("Node0 generates {} blocks".format(nbl), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(nbl)
        self.sync_all()

        # check it is really ceased
        ret = self.nodes[0].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")

        # create a tx with a CSW
        mark_logs("\nCreate a CSWs in a tx withdrawing half the sc balance... ", self.nodes, DEBUG_MODE)

        # CSW sender MC address, in taddress and pub key hash formats
        csw_mc_address = self.nodes[0].getnewaddress()

        sc_balance = self.nodes[0].getscinfo(scid, False, False)['items'][0]['balance']
        sc_csw_amount = (sc_balance/2)
        nullifier = generate_random_field_element_hex()

        # check we have no valid act cert data hash
        try:
            actCertData = self.nodes[0].getactivecertdatahash(scid)['certDataHash']
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            actCertData = None

        ceasingCumScTxCommTree = self.nodes[0].getceasingcumsccommtreehash(scid)['ceasingCumScTxCommTree']

        scid1_swapped = swap_bytes(scid)
        sc_proof = cswMcTest.create_test_proof("sc",
                                               sc_csw_amount,
                                               str(scid1_swapped),
                                               nullifier,
                                               csw_mc_address,
                                               ceasingCumScTxCommTree,
                                               cert_data_hash = actCertData,
                                               constant       = constant1)
        
        sc_csws = [
        {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": nullifier,
            "activeCertData": actCertData,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
            "scProof": sc_proof
        } ]

        # recipient MC address
        taddr_2 = self.nodes[1].getnewaddress()
        sc_csw_tx_outs = {taddr_2: Decimal(sc_csw_amount*4)}

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")

        # check we have an empty act cert data
        decoded_tx = self.nodes[0].decoderawtransaction(sigRawtx['hex'])
        assert_true(len(decoded_tx['vcsw_ccin'][0]['actCertDataHash'])==0)

        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        mark_logs("sent csw tx {}".format(finalRawtx), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Check tx is in mempool...", self.nodes, DEBUG_MODE)
        assert_true(finalRawtx in self.nodes[1].getrawmempool())

        mark_logs("\nNode0 generates 1 block confirming CSW", self.nodes, DEBUG_MODE)
        bl = self.nodes[0].generate(1)[-1]
        self.sync_all()
        
        mark_logs("Check tx is in block just mined...", self.nodes, DEBUG_MODE)
        assert_true(finalRawtx in self.nodes[0].getblock(bl, True)['tx'])

        mark_logs("\nChecking persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        assert_true(self.nodes[0].checkcswnullifier(scid, nullifier)['data'] == 'true')


if __name__ == '__main__':
    CswActCertDataTest().main()
