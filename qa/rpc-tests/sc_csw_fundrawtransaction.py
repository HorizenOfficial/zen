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

from test_framework.test_framework import ForkHeights
from test_framework.mc_test.mc_test import *

from decimal import Decimal
import pprint
import time

NUMB_OF_NODES = 3
DEBUG_MODE = 1
EPOCH_LENGTH = 6
CERT_FEE = Decimal('0.0001')


# Create one-input, one-output, no-fee transaction:
class CswFundrawtransactionTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[['-logtimemicros=1', '-scproofqueuesize=0', '-debug=sc', '-debug=py',
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

        def get_spendable(nodeIdx, min_amount):
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
        Create a SC, advance two epochs and then let it cease.
        Test CSW creation with the use of the cmd fundrawtransaction in different scenarios
        '''

        # prepare some coins 
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()
        prev_epoch_hash = self.nodes[0].getbestblockhash()

        sc_address = "0000000000000000000000000000000000000000000000000000000000000abc"
        sc_epoch_len = EPOCH_LENGTH
        sc_cr_amount = Decimal('21.00000000')

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
        mark_logs("Let 2 epochs pass by...", self.nodes, DEBUG_MODE)

        advance_epoch( certMcTest, self.nodes[0], self.sync_all, scid, "sc1", constant, sc_epoch_len)
        advance_epoch( certMcTest, self.nodes[0], self.sync_all, scid, "sc1", constant, sc_epoch_len)

        mark_logs("Let SC cease... ", self.nodes, DEBUG_MODE)

        nbl = int(sc_epoch_len * 1.5)
        mark_logs("Node0 generates {} blocks".format(nbl), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(nbl)
        self.sync_all()
        print

        # check it is really ceased
        ret = self.nodes[0].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")

        sc_bal = self.nodes[0].getscinfo(scid, False, False)['items'][0]['balance']

        # CSW sender MC address
        csw_mc_address = self.nodes[0].getnewaddress()

        actCertData            = self.nodes[0].getactivecertdatahash(scid)['certDataHash']
        ceasingCumScTxCommTree = self.nodes[0].getceasingcumsccommtreehash(scid)['ceasingCumScTxCommTree']

        scid_swapped = swap_bytes(scid)
        sc_csw_amount = sc_bal/8

        # recipient MC address
        taddr_2 = self.nodes[2].getnewaddress()
        sc_csw_tx_outs = {taddr_2: sc_csw_amount}

        vtxCsw = []

        # -------------------------------------------------------------------- 
        # 1)  One csw input covering exacltly the output 
        mark_logs("One csw input covering exacltly the output...", self.nodes, DEBUG_MODE)

        null1 = generate_random_field_element_hex()

        sc_proof = cswMcTest.create_test_proof("sc1",
                                               sc_csw_amount,
                                               str(scid_swapped),
                                               null1,
                                               csw_mc_address,
                                               ceasingCumScTxCommTree,
                                               cert_data_hash = actCertData,
                                               constant       = constant)

        sc_csws = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": null1,
            "activeCertData": actCertData,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
            "scProof": sc_proof
        }]

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        mark_logs("tx = {}".format(finalRawtx), self.nodes, DEBUG_MODE)
        vtxCsw.append(finalRawtx)
        print
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)
        #pprint.pprint(decoded_tx)

        # vin  - size(1): utxo for paying the fee
        # vout - size(2): recipient of the funds + sender change
        # vcsw_ccin - size(1): CSW funds
        assert_equal(1, len(decoded_tx['vin']))
        assert_equal(2, len(decoded_tx['vout']))
        assert_equal(1, len(decoded_tx['vcsw_ccin']))
        
        assert_true(finalRawtx in self.nodes[2].getrawmempool())

        # -------------------------------------------------------------------- 
        # 2)  One csw input covering half the output 
        mark_logs("One csw input covering half the output...", self.nodes, DEBUG_MODE)

        null2 = generate_random_field_element_hex()

        sc_proof = cswMcTest.create_test_proof("sc1",
                                               sc_csw_amount,
                                               str(scid_swapped),
                                               null2,
                                               csw_mc_address,
                                               ceasingCumScTxCommTree,
                                               cert_data_hash = actCertData,
                                               constant       = constant)

        sc_csws = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": null2,
            "activeCertData": actCertData,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
            "scProof": sc_proof
        }]

        # recipient MC address
        sc_csw_tx_outs = {taddr_2: sc_csw_amount*2}

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        mark_logs("tx = {}".format(finalRawtx), self.nodes, DEBUG_MODE)
        vtxCsw.append(finalRawtx)
        print
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)
        #pprint.pprint(decoded_tx)
        # vin  - size(1): utxo for covering the reminder and paying the fee
        # vout - size(2): recipient of the funds + sender change
        # vcsw_ccin - size(1): CSW funds
        assert_equal(1, len(decoded_tx['vin']))
        assert_equal(2, len(decoded_tx['vout']))
        assert_equal(1, len(decoded_tx['vcsw_ccin']))
        
        assert_true(finalRawtx in self.nodes[2].getrawmempool())

        # --------------------------------------------------------------------------------------------------------- 
        # 3)  One csw input covering half the output, an utxo as input for a small amount and one output as preset change 
        mark_logs("One csw input covering half the output, an utxo as input for a small amount and one output as preset change...", self.nodes, DEBUG_MODE)

        null3 = generate_random_field_element_hex()

        sc_proof = cswMcTest.create_test_proof("sc1",
                                               sc_csw_amount,
                                               str(scid_swapped),
                                               null3,
                                               csw_mc_address,
                                               ceasingCumScTxCommTree,
                                               cert_data_hash = actCertData,
                                               constant       = constant)

        sc_csws = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": null3,
            "activeCertData": actCertData,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
            "scProof": sc_proof
        }]

        # recipient MC address
        taddr_2 = self.nodes[2].getnewaddress()
        sc_csw_tx_outs = {taddr_2: sc_csw_amount*2}

        utx, change = get_spendable(0, Decimal("0.0001"))
        #pprint.pprint(utx)
        #print "Change = ", change
        raw_inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        raw_outs    = { self.nodes[0].getnewaddress() : change, taddr_2: sc_csw_amount*Decimal('1.1') }

        rawtx = self.nodes[0].createrawtransaction(raw_inputs, raw_outs, sc_csws)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        mark_logs("tx = {}".format(finalRawtx), self.nodes, DEBUG_MODE)
        vtxCsw.append(finalRawtx)
        print
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)
        #pprint.pprint(decoded_tx)
        # vin  - size(2): utxo that was set and one for covering the reminder and paying the fee
        # vout - size(3): recipient of the funds + preset change + sender change
        # vcsw_ccin - size(1): CSW funds
        assert_equal(2, len(decoded_tx['vin']))
        assert_equal(3, len(decoded_tx['vout']))
        assert_equal(1, len(decoded_tx['vcsw_ccin']))
        
        assert_true(finalRawtx in self.nodes[2].getrawmempool())

        # --------------------------------------------------------------------------------------------------------- 
        # 4)  One csw input covering all of the output but a small amount to be used as fee. No call to fundrawtransaction
        mark_logs("One csw input covering all of the output but a small amount to be used as fee. No call to fundrawtransaction...", self.nodes, DEBUG_MODE)

        null4 = generate_random_field_element_hex()

        sc_proof = cswMcTest.create_test_proof("sc1",
                                               sc_csw_amount,
                                               str(scid_swapped),
                                               null4,
                                               csw_mc_address,
                                               ceasingCumScTxCommTree,
                                               cert_data_hash = actCertData,
                                               constant       = constant)

        sc_csws = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": null4,
            "activeCertData": actCertData,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
            "scProof": sc_proof
        }]

        # recipient MC address
        taddr_2 = self.nodes[2].getnewaddress()
        sc_csw_tx_outs = {taddr_2: (sc_csw_amount - Decimal("0.0001"))}

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        sigRawtx = self.nodes[0].signrawtransaction(rawtx, None, None, "NONE")
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        mark_logs("tx = {}".format(finalRawtx), self.nodes, DEBUG_MODE)
        vtxCsw.append(finalRawtx)
        print
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)
        #pprint.pprint(decoded_tx)
        # vin  - size(0): no utxo here
        # vout - size(1): recipient of the funds
        # vcsw_ccin - size(1): CSW funds
        assert_equal(0, len(decoded_tx['vin']))
        assert_equal(1, len(decoded_tx['vout']))
        assert_equal(1, len(decoded_tx['vcsw_ccin']))
        
        assert_true(finalRawtx in self.nodes[2].getrawmempool())

        # mine a block for clearing the mempool, we must not cross the limit for csw inputs to a SC
        mark_logs("\nNode0 generates 1 block", self.nodes, DEBUG_MODE)
        bl = self.nodes[0].generate(1)[-1]
        self.sync_all()

        mark_logs("Check all the txes have been included in the block...", self.nodes, DEBUG_MODE)
        blockTxList = self.nodes[0].getblock(bl, True)['tx']
        for entry in vtxCsw:
            assert_true(entry in blockTxList)

        assert_true(len(self.nodes[2].getrawmempool())==0)
        vtxCsw = []

        # --------------------------------------------------------------------------------------------------------- 
        # 5)  Two csw inputs and a sc creation, csws covering only part of the ccoutput 
        mark_logs("Two csw inputs and a sc creation, csws covering only part of the ccoutput...", self.nodes, DEBUG_MODE)

        cr_amount = Decimal("8.0")
        sc_address = "fade"
        vk2 = certMcTest.generate_params("sc2")
        constant2 = generate_random_field_element_hex()

        sc_cr = [{
            "version": 0,
            "epoch_length": EPOCH_LENGTH,
            "amount": cr_amount,
            "address": sc_address,
            "wCertVk": vk2,
            "constant": constant2
        }]

        sc_ft = []

        null5 = generate_random_field_element_hex()
        null6 = generate_random_field_element_hex()

        sc_proof_a = cswMcTest.create_test_proof("sc1",
                                                 sc_csw_amount,
                                                 str(scid_swapped),
                                                 null5,
                                                 csw_mc_address,
                                                 ceasingCumScTxCommTree,
                                                 cert_data_hash = actCertData,
                                                 constant       = constant) 

        sc_proof_b = cswMcTest.create_test_proof("sc1",
                                                 sc_csw_amount,
                                                 str(scid_swapped),
                                                 null6,
                                                 csw_mc_address,
                                                 ceasingCumScTxCommTree,
                                                 cert_data_hash = actCertData,
                                                 constant       = constant) 

        sc_csws = [ {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": null5,
            "activeCertData": actCertData,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
            "scProof": sc_proof_a
        },
        {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": null6,
            "activeCertData": actCertData,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
            "scProof": sc_proof_b
        } ]


        rawtx = self.nodes[1].createrawtransaction([], {}, sc_csws, sc_cr, sc_ft)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        mark_logs("tx = {}".format(finalRawtx), self.nodes, DEBUG_MODE)
        vtxCsw.append(finalRawtx)
        print
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)
        #pprint.pprint(decoded_tx)
        # vin  - size(1): utxo for paying the fee
        # vout - size(1): sender change
        # vccout - size(1): creation output
        # vcsw_ccin - size(2): CSW funds
        assert_equal(1, len(decoded_tx['vin']))
        assert_equal(1, len(decoded_tx['vout']))
        assert_equal(1, len(decoded_tx['vsc_ccout']))
        assert_equal(2, len(decoded_tx['vcsw_ccin']))
        
        assert_true(finalRawtx in self.nodes[2].getrawmempool())


        scid2 = decoded_tx['vsc_ccout'][0]['scid']

        # --------------------------------------------------------------------------------------------------------- 
        # 6)  Two csw inputs and a fw transfer to the latest created sc, csws spending more input than necessary
        mark_logs("Two csw inputs and a fw transfer to the latest created sc, csws spending more input than necessary...", self.nodes, DEBUG_MODE)

        sc_address = "fade"

        sc_cr = []

        sc_ft_amount = Decimal('1.0')
        mc_return_address = self.nodes[0].getnewaddress()
        sc_ft = [{"address": sc_address, "amount": sc_ft_amount, "scid": scid2, "mcReturnAddress": mc_return_address}]

        null7 = generate_random_field_element_hex()
        null8 = generate_random_field_element_hex()

        sc_proof_a = cswMcTest.create_test_proof("sc1",
                                                 sc_csw_amount,
                                                 str(scid_swapped),
                                                 null7,
                                                 csw_mc_address,
                                                 ceasingCumScTxCommTree,
                                                 cert_data_hash = actCertData,
                                                 constant = constant) 

        sc_proof_b = cswMcTest.create_test_proof("sc1",
                                                 sc_csw_amount,
                                                 str(scid_swapped),
                                                 null8,
                                                 csw_mc_address,
                                                 ceasingCumScTxCommTree,
                                                 cert_data_hash = actCertData,
                                                 constant = constant) 

        sc_csws = [
        {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": null7,
            "activeCertData": actCertData,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
            "scProof": sc_proof_a
        },
        {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": null8,
            "activeCertData": actCertData,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
            "scProof": sc_proof_b
        }]

        rawtx = self.nodes[0].createrawtransaction([], {}, sc_csws, sc_cr, sc_ft)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        #pprint.pprint(self.nodes[0].decoderawtransaction(sigRawtx['hex']))
        #raw_input("______________")
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        mark_logs("tx = {}".format(finalRawtx), self.nodes, DEBUG_MODE)
        vtxCsw.append(finalRawtx)
        print
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)
        #pprint.pprint(decoded_tx)
        # vin  - size(0): no utxo needed
        # vout - size(1): recipient of sender change
        # vccout - size(1): fwt output
        # vcsw_ccin - size(2): CSW funds
        assert_equal(0, len(decoded_tx['vin']))
        assert_equal(1, len(decoded_tx['vout']))
        assert_equal(1, len(decoded_tx['vft_ccout']))
        assert_equal(2, len(decoded_tx['vcsw_ccin']))

        assert_true(finalRawtx in self.nodes[2].getrawmempool())

        mark_logs("\nNode0 generates 1 block confirming txes with csw", self.nodes, DEBUG_MODE)
        bl = self.nodes[0].generate(1)[-1]
        self.sync_all()
        
        mark_logs("Check all the txes have been included in the block...", self.nodes, DEBUG_MODE)
        blockTxList = self.nodes[0].getblock(bl, True)['tx']
        for entry in vtxCsw:
            assert_true(entry in blockTxList)

        assert_true(len(self.nodes[2].getrawmempool())==0)


if __name__ == '__main__':
    CswFundrawtransactionTest().main()
