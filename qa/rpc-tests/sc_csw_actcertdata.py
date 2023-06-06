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

NUMB_OF_NODES = 3
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

        if not split:
            # 1 and 2 are joint only if split==false
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:3])
            sync_mempools(self.nodes[1:3])

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        Create two SCs, advance two epochs and then let them cease.
        Test some CSW txes, verifying that active cert data is correctly handled
        Also, the SC creation constant has not been instantiated, therefore cert and csw proof verification
        is tested without using such constant as (optional) parameter.
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
        vk1 = certMcTest.generate_params("sc1", "cert", constant = None)
        vk2 = certMcTest.generate_params("sc2")
        cswVk1 = cswMcTest.generate_params("sc1", "csw", constant = None)
        cswVk2 = cswMcTest.generate_params("sc2")
        constant1 = None
        constant2 = generate_random_field_element_hex()

        sc_cr = []
        sc_cr.append({
            "version": 0,
            "epoch_length": sc_epoch_len,
            "amount": sc_cr_amount,
            "address": sc_address,
            "wCertVk": vk1,
            "wCeasedVk": cswVk1,
            "constant": constant1
        })

        sc_cr.append({
            "version": 0,
            "epoch_length": sc_epoch_len,
            "amount": sc_cr_amount,
            "address": sc_address,
            "wCertVk": vk2,
            "wCeasedVk": cswVk2,
            "constant": constant2
        })

        rawtx = self.nodes[0].createrawtransaction([], {}, [], sc_cr)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        self.sync_all()

        decoded_tx = self.nodes[2].getrawtransaction(finalRawtx, 1)
        scid1 = decoded_tx['vsc_ccout'][0]['scid']
        scid2 = decoded_tx['vsc_ccout'][1]['scid']
        mark_logs("created SC1 id: {}".format(scid1), self.nodes, DEBUG_MODE)
        mark_logs("created SC2 id: {}".format(scid2), self.nodes, DEBUG_MODE)
        print

        # advance two epochs
        mark_logs("\nLet 2 epochs pass by...", self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
            scid1, "sc1", constant1, sc_epoch_len)

        mark_logs("\n==> certificate for SC1 epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
            scid2, "sc2", constant2, sc_epoch_len, generateNumBlocks=0) # do not generate

        mark_logs("\n==> certificate for SC2 epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
             scid1, "sc1", constant1, sc_epoch_len)

        mark_logs("\n==> certificate for SC1 epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
             scid2, "sc2", constant2, sc_epoch_len, generateNumBlocks=0) # do not generate

        mark_logs("\n==> certificate for SC2 epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        # mine one block for having last cert in chain
        mark_logs("\nNode0 generates 1 block confirming last certs", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Let both SCs cease... ".format(scid1), self.nodes, DEBUG_MODE)

        nbl = int(sc_epoch_len * 1.5)
        mark_logs("Node0 generates {} blocks".format(nbl), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(nbl)
        self.sync_all()

        # check they are really ceased
        ret = self.nodes[0].getscinfo(scid1, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")
        ret = self.nodes[0].getscinfo(scid2, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")

        # and have the expected balance
        sc_bal1 = self.nodes[0].getscinfo(scid1, False, False)['items'][0]['balance']
        sc_bal2 = self.nodes[0].getscinfo(scid2, False, False)['items'][0]['balance']

        assert_equal(sc_bal1, sc_cr_amount)
        assert_equal(sc_bal2, sc_cr_amount)

        # create a tx with 3 CSW for sc1 and 1 CSW for sc2
        mark_logs("\nCreate 3 CSWs in a tx withdrawing half the sc balance... ", self.nodes, DEBUG_MODE)

        # CSW sender MC address
        csw_mc_address = self.nodes[0].getnewaddress()

        sc_csw_amount = (sc_bal1/2)/3
        null_1_1 = generate_random_field_element_hex()
        null_1_2 = generate_random_field_element_hex()
        null_1_3 = generate_random_field_element_hex()
        null_2_1 = generate_random_field_element_hex()

        actCertData1 = self.nodes[0].getactivecertdatahash(scid1)['certDataHash']
        actCertData2 = self.nodes[0].getactivecertdatahash(scid2)['certDataHash']

        ceasingCumScTxCommTree1 = self.nodes[0].getceasingcumsccommtreehash(scid1)['ceasingCumScTxCommTree']
        ceasingCumScTxCommTree2 = self.nodes[0].getceasingcumsccommtreehash(scid2)['ceasingCumScTxCommTree']

        scid1_swapped = swap_bytes(scid1)
        sc_proof1_1 = cswMcTest.create_test_proof("sc1",
                                                  sc_csw_amount,
                                                  str(scid1_swapped),
                                                  null_1_1,
                                                  csw_mc_address,
                                                  ceasingCumScTxCommTree1,
                                                  cert_data_hash = actCertData1,
                                                  constant       = constant1)
        
        sc_proof1_2 = cswMcTest.create_test_proof("sc1",
                                                  sc_csw_amount,
                                                  str(scid1_swapped),
                                                  null_1_2,
                                                  csw_mc_address,
                                                  ceasingCumScTxCommTree1,
                                                  cert_data_hash = actCertData1,
                                                  constant       = constant1)

        sc_proof1_3 = cswMcTest.create_test_proof("sc1",
                                                  sc_csw_amount,
                                                  str(scid1_swapped),
                                                  null_1_3,
                                                  csw_mc_address,
                                                  ceasingCumScTxCommTree1,
                                                  cert_data_hash = actCertData1,
                                                  constant       = constant1)

        scid2_swapped = swap_bytes(scid2)
        sc_proof2 = cswMcTest.create_test_proof("sc2",
                                                sc_csw_amount,
                                                str(scid2_swapped),
                                                null_2_1,
                                                csw_mc_address,
                                                ceasingCumScTxCommTree2,
                                                cert_data_hash = actCertData2,
                                                constant       = constant2) 
        #print "sc_proof1 =", sc_proof1
        #print "sc_proof2 =", sc_proof2

        sc_csws = [
        {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid1,
            "epoch": 0,
            "nullifier": null_1_1,
            "activeCertData": actCertData1,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree1,
            "scProof": sc_proof1_1
        },
        {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid1,
            "epoch": 0,
            "nullifier": null_1_2,
            "activeCertData": actCertData1,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree1,
            "scProof": sc_proof1_2
        },
        {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid1,
            "epoch": 0,
            "nullifier": null_1_3,
            "activeCertData": actCertData1,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree1,
            "scProof": sc_proof1_3
        },
        {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid2,
            "epoch": 0,
            "nullifier": null_2_1,
            "activeCertData": actCertData2,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree2,
            "scProof": sc_proof2
        }
        ]

        # recipient MC address
        taddr_2 = self.nodes[2].getnewaddress()
        sc_csw_tx_outs = {taddr_2: Decimal(sc_csw_amount*4)}

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        mark_logs("sent csw tx {}".format(finalRawtx), self.nodes, DEBUG_MODE)
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)

        # vin  - size(1): utxo for paying the fee
        # vout - size(2): recipient of the funds (the same recipient for all the 4 csws) + sender change
        # vcsw_ccin - size(4): CSW funds
        assert_equal(1, len(decoded_tx['vin']))
        assert_equal(2, len(decoded_tx['vout']))
        assert_equal(4, len(decoded_tx['vcsw_ccin']))

        mark_logs("Check tx is in mempool...", self.nodes, DEBUG_MODE)
        assert_true(finalRawtx in self.nodes[2].getrawmempool())

        mark_logs("\nNode0 generates 1 block confirming CSW", self.nodes, DEBUG_MODE)
        bl = self.nodes[0].generate(1)[-1]
        self.sync_all()
        
        mark_logs("Check tx is in block just mined...", self.nodes, DEBUG_MODE)
        assert_true(finalRawtx in self.nodes[0].getblock(bl, True)['tx'])

        mark_logs("Check nullifiers for both sc ids are in MC...", self.nodes, DEBUG_MODE)
        assert_true(self.nodes[0].checkcswnullifier(scid1, null_1_1)['data'] == 'true')
        assert_true(self.nodes[0].checkcswnullifier(scid1, null_1_2)['data'] == 'true')
        assert_true(self.nodes[0].checkcswnullifier(scid1, null_1_3)['data'] == 'true')
        assert_true(self.nodes[0].checkcswnullifier(scid2, null_2_1)['data'] == 'true')

        mark_logs("now create a tx with a csw having a wrong act cert data...", self.nodes, DEBUG_MODE)

        null_1_4 = generate_random_field_element_hex()
        wrong_act_cert_data = generate_random_field_element_hex()
        sc_proof1_4 = cswMcTest.create_test_proof("sc1",
                                                  sc_csw_amount,
                                                  str(scid1_swapped),
                                                  null_1_4,
                                                  csw_mc_address,
                                                  ceasingCumScTxCommTree1,
                                                  cert_data_hash = wrong_act_cert_data,
                                                  constant       = constant1) 

        sc_csws = [ {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid1,
            "epoch": 0,
            "nullifier": null_1_4,
            "activeCertData": wrong_act_cert_data,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree1,
            "scProof": sc_proof1_4
        } ]

        # recipient MC address
        sc_csw_tx_outs = {taddr_2: Decimal(sc_csw_amount)}

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        try:
            self.nodes[0].sendrawtransaction(sigRawtx['hex'])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send csw failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        mark_logs("\nChecking persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        mark_logs("Check nullifiers for both sc ids are in MC...", self.nodes, DEBUG_MODE)
        assert_true(self.nodes[0].checkcswnullifier(scid1, null_1_1)['data'] == 'true')
        assert_true(self.nodes[0].checkcswnullifier(scid1, null_1_2)['data'] == 'true')
        assert_true(self.nodes[0].checkcswnullifier(scid1, null_1_3)['data'] == 'true')
        assert_true(self.nodes[0].checkcswnullifier(scid2, null_2_1)['data'] == 'true')


if __name__ == '__main__':
    CswActCertDataTest().main()
