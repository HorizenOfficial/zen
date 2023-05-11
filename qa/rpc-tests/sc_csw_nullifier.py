#!/usr/bin/env python3
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
    wait_bitcoinds, stop_nodes, sync_mempools, sync_blocks, \
    disconnect_nodes, advance_epoch, swap_bytes
from test_framework.test_framework import ForkHeights
from test_framework.mc_test.mc_test import *

from decimal import Decimal
import pprint

NUMB_OF_NODES = 3
DEBUG_MODE = 1
EPOCH_LENGTH = 6
CERT_FEE = Decimal('0.0001')

ISOLATED_NODE = NUMB_OF_NODES - 1       # index of node to be disconneted from the network
NET_NODES = list(range(NUMB_OF_NODES))
NET_NODES.remove(ISOLATED_NODE)
MAIN_NODE = NET_NODES[0]                # Hardcoded alias, do not change


# Create one-input, one-output, no-fee transaction:
class CswNullifierTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        assert_equal(MAIN_NODE, NET_NODES[0])

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[["-sccoinsmaturity=0", '-scproofqueuesize=0', '-logtimemicros=1', '-debug=sc', '-debug=py',
                                              '-debug=mempool', '-debug=net', '-debug=bench']] * NUMB_OF_NODES)

        for idx in range(NUMB_OF_NODES - 1):
            connect_nodes_bi(self.nodes, idx, idx+1)
        self.is_network_split = False
        self.sync_all()

    def split_network(self):
        # Disconnect the "isolated" node from the network
        idx = ISOLATED_NODE
        if idx == NUMB_OF_NODES - 1:
            disconnect_nodes(self.nodes[idx], idx - 1)
            disconnect_nodes(self.nodes[idx - 1], idx)
        elif idx == 0:
            disconnect_nodes(self.nodes[idx], idx + 1)
            disconnect_nodes(self.nodes[idx + 1], idx)
        else:
            disconnect_nodes(self.nodes[idx], idx - 1)
            disconnect_nodes(self.nodes[idx - 1], idx)
            disconnect_nodes(self.nodes[idx], idx + 1)
            disconnect_nodes(self.nodes[idx + 1], idx)
            connect_nodes_bi(self.nodes, idx - 1, idx + 1)
        self.is_network_split = True

    def join_network(self):
        #Join the (previously split) network pieces together
        idx = ISOLATED_NODE
        if idx == NUMB_OF_NODES - 1:
            connect_nodes_bi(self.nodes, idx, idx - 1)
        elif idx == 0:
            connect_nodes_bi(self.nodes, idx, idx + 1)
        else:
            disconnect_nodes(self.nodes[idx - 1], idx + 1)
            disconnect_nodes(self.nodes[idx + 1], idx - 1)
            connect_nodes_bi(self.nodes, idx, idx - 1)
            connect_nodes_bi(self.nodes, idx, idx + 1)
        self.is_network_split = False

    def run_test(self):
        '''
        Create a SC, advance two epochs and then let it cease.
        Test CSW txes and related in/outputs verifying that nullifiers are properly handled also
        when blocks are disconnected.
        Split the network and test CSW conflicts handling on network rejoining.
        Restare the network and check DB integrity.
        Finally create a second SC, advance 1 epoch only and verify that no CSW can be accepted (max is 2 certs)
        '''

        # prepare some coins 
        self.nodes[MAIN_NODE].generate(ForkHeights['MINIMAL_SC'])
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

        rawtx = self.nodes[MAIN_NODE].createrawtransaction([], {}, [], sc_cr)
        funded_tx = self.nodes[MAIN_NODE].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[MAIN_NODE].signrawtransaction(funded_tx['hex'])
        finalRawtx = self.nodes[MAIN_NODE].sendrawtransaction(sigRawtx['hex'])
        self.sync_all()

        decoded_tx = self.nodes[ISOLATED_NODE].getrawtransaction(finalRawtx, 1)
        scid = decoded_tx['vsc_ccout'][0]['scid']
        mark_logs(f"created SC id: {scid}", self.nodes, DEBUG_MODE)

        # advance two epochs
        mark_logs("\nLet 2 epochs pass by...", self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[MAIN_NODE], self.sync_all,
             scid, "sc1", constant, sc_epoch_len)

        mark_logs(f"\n==> certificate for epoch {epoch_number} {cert}", self.nodes, DEBUG_MODE)


        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[MAIN_NODE], self.sync_all,
             scid, "sc1", constant, sc_epoch_len)

        mark_logs(f"\n==> certificate for epoch {epoch_number} {cert}", self.nodes, DEBUG_MODE)


        # mine one block for having last cert in chain
        mark_logs(f"\nNode {MAIN_NODE} generates 1 block confirming last cert", self.nodes, DEBUG_MODE)
        self.nodes[MAIN_NODE].generate(1)
        self.sync_all()

        # check we have cert data hash for the last active certificate
        mark_logs("\nCheck we have expected cert data hashes", self.nodes, DEBUG_MODE)
        try:
            assert_true(self.nodes[MAIN_NODE].getactivecertdatahash(scid)['certDataHash'])
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(f"{errorString}", self.nodes, DEBUG_MODE)
            assert (False)

        mark_logs("Let SC cease... ", self.nodes, DEBUG_MODE)

        nbl = int(sc_epoch_len * 1.5)
        mark_logs(f"Node {MAIN_NODE} generates {nbl} blocks", self.nodes, DEBUG_MODE)
        self.nodes[MAIN_NODE].generate(nbl)
        self.sync_all()

        # check it is really ceased
        ret = self.nodes[MAIN_NODE].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")

        # and has the expected balance
        sc_bal = self.nodes[MAIN_NODE].getscinfo(scid, False, False)['items'][0]['balance']
        assert_equal(sc_bal, sc_cr_amount)

        mark_logs("\nCreate a CSW withdrawing half the sc balance... ", self.nodes, DEBUG_MODE)

        # CSW sender MC address
        csw_mc_address = self.nodes[MAIN_NODE].getnewaddress()

        sc_csw_amount = sc_bal/2
        null1 = generate_random_field_element_hex()
        actCertData = self.nodes[MAIN_NODE].getactivecertdatahash(scid)['certDataHash']
        print(f"Active Cert Data Hash: -------> {actCertData}")

        ceasingCumScTxCommTree = self.nodes[MAIN_NODE].getceasingcumsccommtreehash(scid)['ceasingCumScTxCommTree']

        scid_swapped = str(swap_bytes(scid))
        sc_proof1 = cswMcTest.create_test_proof("sc1",
                                                sc_csw_amount,
                                                scid_swapped,
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
            "scProof": sc_proof1
        }]

        # recipient MC address
        taddr_2 = self.nodes[ISOLATED_NODE].getnewaddress()
        sc_csw_tx_outs = {taddr_2: sc_csw_amount}

        rawtx = self.nodes[MAIN_NODE].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[MAIN_NODE].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[MAIN_NODE].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        finalRawtx = self.nodes[MAIN_NODE].sendrawtransaction(sigRawtx['hex'])
        mark_logs(f"sent csw 1 {finalRawtx} retrieving {sc_csws[0]['amount']} coins on Node {ISOLATED_NODE} behalf", self.nodes, DEBUG_MODE)
        self.sync_all()

        decoded_tx = self.nodes[NET_NODES[1]].getrawtransaction(finalRawtx, 1)
        # vin  - size(1): utxo for paying the fee
        # vout - size(2): recipient of the funds + sender change
        # vcsw_ccin - size(1): CSW funds
        assert_equal(1, len(decoded_tx['vin']))
        assert_equal(2, len(decoded_tx['vout']))
        assert_equal(1, len(decoded_tx['vcsw_ccin']))

        mark_logs("Check csw is in mempool...", self.nodes, DEBUG_MODE)
        assert_true(finalRawtx in self.nodes[ISOLATED_NODE].getrawmempool())

        mark_logs(f"\nNode {MAIN_NODE} generates 1 block confirming CSW", self.nodes, DEBUG_MODE)
        bl = self.nodes[MAIN_NODE].generate(1)[-1]
        self.sync_all()

        mark_logs("Check csw is in block just mined...", self.nodes, DEBUG_MODE)
        assert_true(finalRawtx in self.nodes[MAIN_NODE].getblock(bl, True)['tx'])

        mark_logs("Check nullifier is in MC...", self.nodes, DEBUG_MODE)
        res = self.nodes[MAIN_NODE].checkcswnullifier(scid, null1)
        assert_equal(res['data'], 'true')

        n2_bal = self.nodes[ISOLATED_NODE].getbalance()
        mark_logs(f"Check Node {ISOLATED_NODE} has the expected balance...", self.nodes, DEBUG_MODE)
        assert_equal(n2_bal, sc_csw_amount)

        amount_2_1 = Decimal(n2_bal)/Decimal('4.0')
        mark_logs(f"\nNode {ISOLATED_NODE} sends {amount_2_1} coins to Node {NET_NODES[1]} using csw funds...", self.nodes, DEBUG_MODE)
        tx = self.nodes[ISOLATED_NODE].sendtoaddress(self.nodes[NET_NODES[1]].getnewaddress(), amount_2_1)
        mark_logs(f"tx = {tx}", self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs(f"\nNode {MAIN_NODE} generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[MAIN_NODE].generate(1)
        self.sync_all()


        mark_logs(f"\nNode {ISOLATED_NODE} invalidate its chain from csw block on...", self.nodes, DEBUG_MODE)
        self.nodes[ISOLATED_NODE].invalidateblock(bl)
        sync_mempools([self.nodes[i] for i in NET_NODES])

        mark_logs(f"Check csw has been put back in Node {ISOLATED_NODE} mempool...", self.nodes, DEBUG_MODE)
        assert_true(tx, finalRawtx in self.nodes[ISOLATED_NODE].getrawmempool())

        mark_logs(f"Check Node {ISOLATED_NODE} has null confirmed balance...", self.nodes, DEBUG_MODE)
        n2_bal = self.nodes[ISOLATED_NODE].z_gettotalbalance()['total']
        assert_equal(Decimal(n2_bal), Decimal('0.0'))

        mark_logs(f"Check nullifier is no more in MC from Node {ISOLATED_NODE} perspective...", self.nodes, DEBUG_MODE)
        res = self.nodes[ISOLATED_NODE].checkcswnullifier(scid, null1)
        assert_equal(res['data'], 'false')

        mark_logs(f"\nNode {ISOLATED_NODE} reconsider last invalidated blocks", self.nodes, DEBUG_MODE)
        self.nodes[ISOLATED_NODE].reconsiderblock(bl)
        self.sync_all()


        mark_logs(f"Check nullifier is back in MC from Node {ISOLATED_NODE} perspective...", self.nodes, DEBUG_MODE)
        res = self.nodes[ISOLATED_NODE].checkcswnullifier(scid, null1)
        assert_equal(res['data'], 'true')

        # Try to create CSW with the nullifier which already exists in the chain
        mark_logs("\nTrying to send a csw with the same nullifier (expecting failure...)", self.nodes, DEBUG_MODE)
        sc_csw_amount = sc_bal/4
        sc_csws[0]['amount'] = sc_csw_amount
        sc_csw_tx_outs = {taddr_2: sc_csw_amount}
        rawtx = self.nodes[MAIN_NODE].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[MAIN_NODE].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[MAIN_NODE].signrawtransaction(funded_tx['hex'])
        try:
            self.nodes[MAIN_NODE].sendrawtransaction(sigRawtx['hex'])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(f"Send csw failed with reason {errorString}", self.nodes, DEBUG_MODE)


        mark_logs("\nCreate a CSW withdrawing un third of the original sc balance... ", self.nodes, DEBUG_MODE)
        sc_csw_amount = sc_bal/3
        sc_csw_tx_outs = {taddr_2: sc_csw_amount}
        null2 = generate_random_field_element_hex()

        scid_swapped = str(swap_bytes(scid))
        sc_proof2 = cswMcTest.create_test_proof("sc1",
                                                sc_csw_amount,
                                                scid_swapped,
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
            "scProof": sc_proof2
        }]

        try:
            rawtx = self.nodes[MAIN_NODE].createrawtransaction([], sc_csw_tx_outs, sc_csws)
            funded_tx = self.nodes[MAIN_NODE].fundrawtransaction(rawtx)
            sigRawtx = self.nodes[MAIN_NODE].signrawtransaction(funded_tx['hex'])
            finalRawTx = self.nodes[MAIN_NODE].sendrawtransaction(sigRawtx['hex'])
            mark_logs(f"sent csw 2 {finalRawtx} retrieving {sc_csw_amount} coins on Node {ISOLATED_NODE} behalf", self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(f"Send csw failed with reason {errorString}", self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs(f"\nNode {MAIN_NODE} generates 1 block confirming CSW", self.nodes, DEBUG_MODE)
        self.nodes[MAIN_NODE].generate(1)
        self.sync_all()

        n1_bal = self.nodes[NET_NODES[1]].z_gettotalbalance()['total']
        n2_bal = self.nodes[ISOLATED_NODE].z_gettotalbalance()['total']
        mark_logs(f"Node {NET_NODES[1]} has {n1_bal} confirmed balance", self.nodes, DEBUG_MODE)
        mark_logs(f"Node {ISOLATED_NODE} has {n2_bal} confirmed balance", self.nodes, DEBUG_MODE)


        #============================================================================================
        mark_logs("\nSplit network", self.nodes, DEBUG_MODE)
        self.split_network()
        mark_logs(f"The network is split: {NET_NODES[0]}-{NET_NODES[1]} .. {ISOLATED_NODE}", self.nodes, DEBUG_MODE)

        # Network part 0-1
        #------------------
        # Node0 create CSW Tx with a new unused nullifier
        mark_logs("\nCreate a CSW withdrawing one sixth of the original sc balance... ", self.nodes, DEBUG_MODE)
        sc_csw_amount = sc_bal/6
        null_n0 = generate_random_field_element_hex()
        taddr_1 = self.nodes[NET_NODES[1]].getnewaddress()
        sc_csw_tx_outs_1 = {taddr_1: sc_csw_amount}

        sc_proof_n0 = cswMcTest.create_test_proof("sc1",
                                                  sc_csw_amount,
                                                  scid_swapped,
                                                  null_n0,
                                                  csw_mc_address,
                                                  ceasingCumScTxCommTree,
                                                  cert_data_hash = actCertData,
                                                  constant       = constant) 

        sc_csws = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": null_n0,
            "activeCertData": actCertData,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
            "scProof": sc_proof_n0
        }]

        try:
            rawtx = self.nodes[MAIN_NODE].createrawtransaction([], sc_csw_tx_outs_1, sc_csws)
            funded_tx = self.nodes[MAIN_NODE].fundrawtransaction(rawtx)
            sigRawtx = self.nodes[MAIN_NODE].signrawtransaction(funded_tx['hex'])
            tx_n1 = self.nodes[MAIN_NODE].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(f"Send csw failed with reason {errorString}", self.nodes, DEBUG_MODE)
            assert(False)

        sync_mempools([self.nodes[i] for i in NET_NODES])
        mark_logs(f"Node {MAIN_NODE} sent csw {tx_n1} retrieving {sc_csws[0]['amount']} coins on Node {NET_NODES[1]} behalf", self.nodes, DEBUG_MODE)

        self.nodes[MAIN_NODE].generate(1)
        sync_blocks([self.nodes[i] for i in NET_NODES])
        sync_mempools([self.nodes[i] for i in NET_NODES])

        n1_bal = self.nodes[NET_NODES[1]].z_gettotalbalance()['total']
        mark_logs(f"Node {NET_NODES[1]} has {n1_bal} confirmed balance", self.nodes, DEBUG_MODE)
        assert_equal(Decimal(n1_bal), Decimal(amount_2_1 + sc_bal/6)) 

        # Node1 sends all of its balance to Node0
        amount = Decimal(n1_bal) - Decimal("0.0001")
        tx_1_0 = self.nodes[NET_NODES[1]].sendtoaddress(self.nodes[MAIN_NODE].getnewaddress(), amount)
        mark_logs(f"tx = {tx_1_0}", self.nodes, DEBUG_MODE)
        mark_logs(f"Node {NET_NODES[1]} sent {amount} coins to Node {MAIN_NODE} using csw funds...", self.nodes, DEBUG_MODE)
        sync_mempools([self.nodes[i] for i in NET_NODES])

        mark_logs("Check tx is in mempool", self.nodes, DEBUG_MODE)
        assert_true(tx_1_0 in self.nodes[MAIN_NODE].getrawmempool()) 
        assert_true(tx_1_0 in self.nodes[NET_NODES[1]].getrawmempool()) 

        mark_logs(f"Check tx is in Node {MAIN_NODE} wallet", self.nodes, DEBUG_MODE)
        utxos = self.nodes[MAIN_NODE].listunspent(0, 0)
        assert_equal(tx_1_0, utxos[0]['txid']) 

        # check we have emptied the SC balance
        sc_bal_n0 = self.nodes[MAIN_NODE].getscinfo(scid)['items'][0]['balance']
        mark_logs(f"sc balance from Node {MAIN_NODE} view is: {sc_bal_n0}", self.nodes, DEBUG_MODE)
        assert_equal(Decimal(sc_bal_n0), Decimal('0.0'))

        print
        # Network part 2
        #------------------
        mark_logs("Create a CSW withdrawing one twelfth of the original sc balance... ", self.nodes, DEBUG_MODE)
        sc_csw_amount = sc_bal/12
        sc_csw_tx_outs = {taddr_2: sc_csw_amount}
        null_n2 = generate_random_field_element_hex()
        csw_mc_address = self.nodes[ISOLATED_NODE].getnewaddress()

        sc_proof_n2 = cswMcTest.create_test_proof("sc1",
                                                  sc_csw_amount,
                                                  scid_swapped,
                                                  null_n2,
                                                  csw_mc_address,
                                                  ceasingCumScTxCommTree,
                                                  cert_data_hash = actCertData,
                                                  constant       = constant) 

        sc_csws = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": null_n2,
            "activeCertData": actCertData,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
            "scProof": sc_proof_n2
        }]

        try:
            rawtx = self.nodes[ISOLATED_NODE].createrawtransaction([], sc_csw_tx_outs, sc_csws)
            funded_tx = self.nodes[ISOLATED_NODE].fundrawtransaction(rawtx)
            sigRawtx = self.nodes[ISOLATED_NODE].signrawtransaction(funded_tx['hex'])
            tx_n2 = self.nodes[ISOLATED_NODE].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(f"Send csw failed with reason {errorString}", self.nodes, DEBUG_MODE)
            assert(False)

        sync_mempools([self.nodes[ISOLATED_NODE]])

        mark_logs(f"Node {ISOLATED_NODE} sent csw {tx_n2} retrieving {sc_csws[0]['amount']} coins for himself", self.nodes, DEBUG_MODE)

        self.nodes[ISOLATED_NODE].generate(1)
        sync_mempools([self.nodes[ISOLATED_NODE]])
        sync_blocks([self.nodes[ISOLATED_NODE]])

        n2_bal = self.nodes[ISOLATED_NODE].z_gettotalbalance()['total']
        mark_logs(f"Node {ISOLATED_NODE} has {n2_bal} confirmed balance", self.nodes, DEBUG_MODE)

        # check we have still 1.0 coin in the the SC balance (12-6-4-1=1)
        sc_bal_n2 = self.nodes[ISOLATED_NODE].getscinfo(scid)['items'][0]['balance']
        mark_logs(f"sc balance from Node {ISOLATED_NODE} view is: {sc_bal_n2}", self.nodes, DEBUG_MODE)
        assert_equal(Decimal(sc_bal_n2), Decimal('1.0'))


        #============================================================================================
        mark_logs("\nJoining network", self.nodes, DEBUG_MODE)
        self.join_network()
        mark_logs("Network joined", self.nodes, DEBUG_MODE)

        self.nodes[ISOLATED_NODE].generate(5)
        self.sync_all()

        # Node2 should have prevailed therefore nullifier n0 should have disappeared
        mark_logs(f"Check nullifier used by Node {MAIN_NODE} is not in MC...", self.nodes, DEBUG_MODE)
        res = self.nodes[ISOLATED_NODE].checkcswnullifier(scid, null_n0)
        assert_equal(res['data'], 'false')

        mark_logs(f"Check nullifier used by Node {ISOLATED_NODE} is in MC also from Node {MAIN_NODE} perspective...", self.nodes, DEBUG_MODE)
        res = self.nodes[MAIN_NODE].checkcswnullifier(scid, null_n2)
        assert_equal(res['data'], 'true')

        mark_logs(f"Check tx {tx_1_0} is no more in mempool", self.nodes, DEBUG_MODE)
        assert_false(tx_1_0 in self.nodes[MAIN_NODE].getrawmempool()) 
        assert_false(tx_1_0 in self.nodes[NET_NODES[1]].getrawmempool()) 

        mark_logs(f"Check it is in no more in Node {MAIN_NODE} wallet too", self.nodes, DEBUG_MODE)
        utxos = self.nodes[MAIN_NODE].listunspent(0)
        for x in utxos:
            if x['txid'] == tx_1_0:
                assert_true(False)

        n1_bal = self.nodes[NET_NODES[1]].z_gettotalbalance()['total']
        mark_logs(f"Node {NET_NODES[1]} has {n1_bal} confirmed balance", self.nodes, DEBUG_MODE)
        assert_equal(Decimal(n1_bal), Decimal(amount_2_1)) 

        mark_logs("Checking mempools...", self.nodes, DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            assert_equal(0, len(self.nodes[i].getrawmempool()))

        mark_logs("Checking sc balance...", self.nodes, DEBUG_MODE)
        sc_bal_n0 = self.nodes[MAIN_NODE].getscinfo(scid)['items'][0]['balance']
        sc_bal_n1 = self.nodes[NET_NODES[1]].getscinfo(scid)['items'][0]['balance']
        sc_bal_n2 = self.nodes[ISOLATED_NODE].getscinfo(scid)['items'][0]['balance']
        assert_equal(Decimal('1.0'), sc_bal_n0)
        assert_equal(sc_bal_n1, sc_bal_n0)
        assert_equal(sc_bal_n2, sc_bal_n0)

        n0_bal = self.nodes[MAIN_NODE].z_gettotalbalance()['total']
        n1_bal = self.nodes[NET_NODES[1]].z_gettotalbalance()['total']
        n2_bal = self.nodes[ISOLATED_NODE].z_gettotalbalance()['total']

        mark_logs("\nChecking persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        mark_logs("Checking nodes balance...", self.nodes, DEBUG_MODE)
        assert_equal(n0_bal, self.nodes[MAIN_NODE].z_gettotalbalance()['total'])
        assert_equal(n1_bal, self.nodes[NET_NODES[1]].z_gettotalbalance()['total'])
        assert_equal(n2_bal, self.nodes[ISOLATED_NODE].z_gettotalbalance()['total'])

        mark_logs("Checking sc balance...", self.nodes, DEBUG_MODE)
        assert_equal(sc_bal_n0, self.nodes[MAIN_NODE].getscinfo(scid)['items'][0]['balance'])
        assert_equal(sc_bal_n1, self.nodes[NET_NODES[1]].getscinfo(scid)['items'][0]['balance'])
        assert_equal(sc_bal_n2, self.nodes[ISOLATED_NODE].getscinfo(scid)['items'][0]['balance'])

        mark_logs("Checking nullifiers...", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[MAIN_NODE].checkcswnullifier(scid, null1)['data'], 'true')
        assert_equal(self.nodes[MAIN_NODE].checkcswnullifier(scid, null2)['data'], 'true')
        assert_equal(self.nodes[MAIN_NODE].checkcswnullifier(scid, null_n0)['data'], 'false')
        assert_equal(self.nodes[MAIN_NODE].checkcswnullifier(scid, null_n2)['data'], 'true')

        mark_logs("\nVerify we need a valid active cert data hash  for a CSW to be legal...", self.nodes, DEBUG_MODE)

        vk2 = certMcTest.generate_params("sc2")
        cswVk2 = cswMcTest.generate_params("sc2")
        constant2 = generate_random_field_element_hex()
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": sc_epoch_len,
            "toaddress": "dada",
            "amount": sc_cr_amount,
            "wCertVk": vk2,
            "constant": constant2,
            'customData': "abcdef",
            'wCeasedVk': cswVk2,
        }

        ret = self.nodes[MAIN_NODE].sc_create(cmdInput)
        creating_tx = ret['txid']
        mark_logs(f"Node {MAIN_NODE} created SC spending {sc_cr_amount} coins via tx1 {creating_tx}", self.nodes, DEBUG_MODE)
        self.sync_all()
        scid2 = self.nodes[MAIN_NODE].getrawtransaction(creating_tx, 1)['vsc_ccout'][0]['scid']
        mark_logs(f"==> created SC ids {scid2}", self.nodes, DEBUG_MODE)
            
        # advance two epochs and cease it
        mark_logs("\nLet 2 epochs pass by...", self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[MAIN_NODE], self.sync_all,
            scid2, "sc2", constant2, sc_epoch_len)

        mark_logs(f"\n==> certificate for epoch {epoch_number} {cert}", self.nodes, DEBUG_MODE)
        

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[MAIN_NODE], self.sync_all,
            scid2, "sc2", constant2, sc_epoch_len)

        mark_logs(f"\n==> certificate for epoch {epoch_number} {cert}", self.nodes, DEBUG_MODE)

        mark_logs("Let SC cease... ", self.nodes, DEBUG_MODE)

        nbl = int(sc_epoch_len * 1.5)
        mark_logs(f"Node {MAIN_NODE} generates {nbl} blocks", self.nodes, DEBUG_MODE)
        self.nodes[MAIN_NODE].generate(nbl)
        self.sync_all()

        # check it is really ceased
        ret = self.nodes[MAIN_NODE].getscinfo(scid2, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")

        # and has the expected balance
        sc_bal = self.nodes[MAIN_NODE].getscinfo(scid2, False, False)['items'][0]['balance']
        assert_equal(sc_bal, sc_cr_amount)

        # Try to create a CSW with a wrong act cert data hash
        mark_logs("\nTrying to send a csw with a wrong active cert data hash (expecting failure...)", self.nodes, DEBUG_MODE)
        csw_mc_address = self.nodes[MAIN_NODE].getnewaddress()
        sc_csw_amount = sc_bal
        null3 = generate_random_field_element_hex()
        actCertData3 = generate_random_field_element_hex()
        ceasingCumScTxCommTree2 = self.nodes[MAIN_NODE].getceasingcumsccommtreehash(scid2)['ceasingCumScTxCommTree']

        scid2_swapped = str(swap_bytes(scid2))
        sc_proof2 = cswMcTest.create_test_proof("sc2",
                                                sc_csw_amount,
                                                scid2_swapped,
                                                null3,
                                                csw_mc_address,
                                                ceasingCumScTxCommTree2,
                                                cert_data_hash = actCertData3,
                                                constant       = constant2) 

        sc_csws = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid2,
            "epoch": 0,
            "nullifier": null3,
            "activeCertData": actCertData3,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree2,
            "scProof": sc_proof2
        }]

        out_amount = sc_csw_amount / Decimal("2.0")
        sc_csw_tx_outs = {taddr_2: out_amount}
        rawtx = self.nodes[MAIN_NODE].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[MAIN_NODE].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[MAIN_NODE].signrawtransaction(funded_tx['hex'])
        try:
            tx = self.nodes[MAIN_NODE].sendrawtransaction(sigRawtx['hex'])
            pprint.pprint(self.nodes[MAIN_NODE].getrawtransaction(tx, 1))
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(f"Send csw failed with reason {errorString}", self.nodes, DEBUG_MODE)

        # check we have all cert data hash for both sc ids
        mark_logs("Check we have an active cert data hashe for sidechain 1", self.nodes, DEBUG_MODE)
        try:
            assert_true(self.nodes[NET_NODES[1]].getactivecertdatahash(scid)['certDataHash'])
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(f"{errorString}", self.nodes, DEBUG_MODE)
            assert (False)

        ret = self.nodes[MAIN_NODE].getscinfo(scid2, False, True)['items'][0]
        assert_equal(ret['state'], "CEASED")
        assert_equal(ret['lastCertificateEpoch'], 1)

        try:
            assert_true(self.nodes[NET_NODES[1]].getactivecertdatahash(scid2)['certDataHash'])
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(f"{errorString}", self.nodes, DEBUG_MODE)
            assert (False)


if __name__ == '__main__':
    CswNullifierTest().main()
