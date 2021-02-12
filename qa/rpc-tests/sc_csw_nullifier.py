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
    disconnect_nodes, advance_epoch

from test_framework.mc_test.mc_test import *

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
                                 extra_args=[["-sccoinsmaturity=0", '-logtimemicros=1', '-debug=sc', '-debug=py',
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
        Test CSW txes and related in/outputs verifying that nullifiers are properly handled also
        when blocks are disconnected.
        Split the network and test CSW conflicts handling on network rejoining.
        Restare the network and check DB integrity.
        Finally create a second SC, advance 1 epoch only and verify that no CSW can be accepted (max is 2 certs)
        '''

        # prepare some coins 
        self.nodes[0].generate(220)
        self.sync_all()
        prev_epoch_hash = self.nodes[0].getbestblockhash()

        sc_address = "0000000000000000000000000000000000000000000000000000000000000abc"
        sc_epoch_len = EPOCH_LENGTH
        sc_cr_amount = Decimal('12.00000000')

        mcTest = MCTestUtils(self.options.tmpdir, self.options.srcdir)

        # generate wCertVk and constant
        vk = mcTest.generate_params("sc1")
        cswVk = mcTest.generate_params("csw1")
        constant = generate_random_field_element_hex()

        sc_cr = []
        sc_cr.append({

            "epoch_length": sc_epoch_len,
            "amount": sc_cr_amount,
            "address": sc_address,
            "wCertVk": vk,
            "wCeasedVk": cswVk,
            "constant": constant
        })

        rawtx = self.nodes[0].createrawtransaction([], {}, [], sc_cr, [])
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        self.sync_all()

        decoded_tx = self.nodes[2].getrawtransaction(finalRawtx, 1)
        scid = decoded_tx['vsc_ccout'][0]['scid']
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)
        print

        # advance two epochs
        mark_logs("\nLet 2 epochs pass by...".  format(sc_epoch_len), self.nodes, DEBUG_MODE)

        cert, epoch_block_hash, epoch_number = advance_epoch(
            mcTest, self.nodes[0], self.sync_all,
             scid, prev_epoch_hash, "sc1", constant, sc_epoch_len)

        mark_logs("\n==> certificate for epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        prev_epoch_hash = epoch_block_hash

        cert, epoch_block_hash, epoch_number = advance_epoch(
            mcTest, self.nodes[0], self.sync_all,
             scid, prev_epoch_hash, "sc1", constant, sc_epoch_len)

        mark_logs("\n==> certificate for epoch {} {}l".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        prev_epoch_hash = epoch_block_hash

        # mine one block for having last cert in chain
        mark_logs("\nNode0 generates 1 block confirming last cert", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # check we have cert data hash for the last active certificate
        mark_logs("\nCheck we have expected cert data hashes", self.nodes, DEBUG_MODE)
        try:
            assert_true(self.nodes[0].getactivecertdatahash(scid)['certDataHash'])
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("{}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        mark_logs("Let SC cease... ".format(scid), self.nodes, DEBUG_MODE)

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

        mark_logs("\nCreate a CSW withdrawing half the sc balance... ", self.nodes, DEBUG_MODE)

        # CSW sender MC address
        csw_mc_address = self.nodes[0].getnewaddress()
        sc_csw_amount = sc_bal/2
        null1 = generate_random_field_element_hex()

        sc_csws = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": null1,
            # Temp hardcoded proof with valid structure TODO: generate a real valid CSW proof
            "scProof": "927e725a39f1c219a458f02d27fb327cc9595985ed947553d979261261b96360b23633b747df8141bcb12076b75f654c35ba0869df74a236763fe0c070e6da2959c1a8c77330783e76e4ad5801818c5edb06567196813355bea5e08beaa5010088965b13b48cbf962106500727ba05b31b4f429076230a90384d18b0e5f395a87ea466704a56375d3a68e65777568881b432208029c12cda5d089f596cf91da14392ed6c619c195a6bebe04c2caba17443906fbf386bc4555b0b721a1ead0000007acd59b470379a38d8de9e82b54fdd1e4e8bd8b2059b62552814989c25f7e07c6261ebc6de8b4b875893a874df953594beb119d53fd74e33e09cb66ed717c393c3fd22f1b465332a17c3d934172fdd33d1c641a9121c5e762b6e59305d1d0100ec5aed56c4290c6bb57e1d1b5b2b1f861f9926403446482f72cede346c0feae2817a2f18b7a37a9b55a3e9deb2a555ffb0d9331cb320ce18aa99a2c2c025c3d28afc77c631263b91160b1f556a6d1d158a8d3c56ab61dc9396e6536094720000741ae2c1569b098231dce089680fb1e561d974ce4f4e00cbe1150281ce12dd561be12a7fefcb30f62d3c8934926ae4eb4a4cb4378dd2568648ff12a7c36302be4d5a578dc360a3125b0c1427fb6b55a067f01d24d616c954bce363a8ef1001003a1ebe119da0561bf1d3294819759677fbd37dbac403662e263bfa71a4992228557a31d2d9ce0a7ffcbe91aa57f38cae7b51ef2681b16f275c0f87c89fbc2060690ac77dc1d3d20b7d3c6b5af1c92ee96e61b6635e343c3976112eb4ec91000000fe60207ddf86be08604c41f46f2e3740b479cad9fd1cb5f8c589595ba3d50f6c3984bbe707d460a0e27d4ec90d89a3476c647a6ea262b910dcb267325c375c713ff7031fe3a200130060bf09900e2e5244f88355a2a0587b068caae7f65b01005f0fb082380604a78c66e21681c2c7f3f59042c7b4495435b8d972bbb535ae8dd09ea8232b0161dc3a13f4a718b5a7fa4cb01d6625e38d73032baf3a9ffcff5a7493a27eeab25c97bee8eddf2fd2c9e9dd1bd1813c22b046c01caccc7478000000"
        }]

        # recipient MC address
        taddr_2 = self.nodes[2].getnewaddress()
        sc_csw_tx_outs = {taddr_2: sc_csw_amount}

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws, [], [])
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        mark_logs("sent csw 1 {} retrieving {} coins on Node2 behalf".format(finalRawtx, sc_csws[0]['amount']), self.nodes, DEBUG_MODE)
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)
        # vin  - size(1): utxo for paying the fee
        # vout - size(2): recipient of the funds + sender change
        # vcsw_ccin - size(1): CSW funds
        assert_equal(1, len(decoded_tx['vin']))
        assert_equal(2, len(decoded_tx['vout']))
        assert_equal(1, len(decoded_tx['vcsw_ccin']))

        mark_logs("Check csw is in mempool...", self.nodes, DEBUG_MODE)
        assert_true(finalRawtx in self.nodes[2].getrawmempool())

        mark_logs("\nNode0 generates 1 block confirming CSW", self.nodes, DEBUG_MODE)
        bl = self.nodes[0].generate(1)[-1]
        self.sync_all()

        
        mark_logs("Check csw is in block just mined...", self.nodes, DEBUG_MODE)
        assert_true(finalRawtx in self.nodes[0].getblock(bl, True)['tx'])

        mark_logs("Check nullifier is in MC...", self.nodes, DEBUG_MODE)
        res = self.nodes[0].checkcswnullifier(scid, null1)
        assert_equal(res['data'], 'true')

        n2_bal = self.nodes[2].getbalance()
        mark_logs("Check Node2 has the expected balance...", self.nodes, DEBUG_MODE)
        assert_equal(n2_bal, sc_csw_amount)

        amount_2_1 = Decimal(n2_bal)/Decimal('4.0')
        mark_logs("\nNode2 sends {} coins to Node1 using csw funds...".format(amount_2_1), self.nodes, DEBUG_MODE)
        tx = self.nodes[2].sendtoaddress(self.nodes[1].getnewaddress(), amount_2_1);
        mark_logs("tx = {}".format(tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        
        mark_logs("\nNode2 invalidate its chain from csw block on...", self.nodes, DEBUG_MODE)
        self.nodes[2].invalidateblock(bl)
        sync_mempools(self.nodes[0:2])

        mark_logs("Check csw has been put back in Node2 mempool...", self.nodes, DEBUG_MODE)
        assert_true(tx, finalRawtx in self.nodes[2].getrawmempool())

        mark_logs("Check Node2 has null confirmed balance...", self.nodes, DEBUG_MODE)
        n2_bal = self.nodes[2].z_gettotalbalance()['total']
        assert_equal(Decimal(n2_bal), Decimal('0.0'))

        mark_logs("Check nullifier is no more in MC from Node2 perspective...", self.nodes, DEBUG_MODE)
        res = self.nodes[2].checkcswnullifier(scid, null1)
        assert_equal(res['data'], 'false')

        mark_logs("\nNode2 reconsider last invalidated blocks", self.nodes, DEBUG_MODE)
        self.nodes[2].reconsiderblock(bl)
        self.sync_all()

        
        mark_logs("Check nullifier is back in MC from Node2 perspective...", self.nodes, DEBUG_MODE)
        res = self.nodes[2].checkcswnullifier(scid, null1)
        assert_equal(res['data'], 'true')

        # Try to create CSW with the nullifier which already exists in the chain
        mark_logs("\nTrying to send a csw with the same nullifier (expecting failure...)", self.nodes, DEBUG_MODE)
        sc_csw_amount = sc_bal/4
        sc_csws[0]['amount'] = sc_csw_amount
        sc_csw_tx_outs = {taddr_2: sc_csw_amount}
        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws, [], [])
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
        try:
            self.nodes[0].sendrawtransaction(sigRawtx['hex'])
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send csw failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)


        mark_logs("\nCreate a CSW withdrawing un third of the original sc balance... ", self.nodes, DEBUG_MODE)
        sc_csw_amount = sc_bal/3
        sc_csws[0]['amount'] = sc_csw_amount
        sc_csw_tx_outs = {taddr_2: sc_csw_amount}
        null2 = generate_random_field_element_hex()
        sc_csws[0]['nullifier'] = null2
        try:
            rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws, [], [])
            funded_tx = self.nodes[0].fundrawtransaction(rawtx)
            sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
            finalRawTx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
            mark_logs("sent csw 2 {} retrieving {} coins on Node2 behalf".format(finalRawtx, sc_csw_amount), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send csw failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("\nNode0 generates 1 block confirming CSW", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        n1_bal = self.nodes[1].z_gettotalbalance()['total']
        n2_bal = self.nodes[2].z_gettotalbalance()['total']
        mark_logs("Node1 has {} confirmed balance".format(n1_bal), self.nodes, DEBUG_MODE)
        mark_logs("Node2 has {} confirmed balance".format(n2_bal), self.nodes, DEBUG_MODE)
        

        #============================================================================================
        mark_logs("\nSplit network", self.nodes, DEBUG_MODE)
        self.split_network()
        mark_logs("The network is split: 0-1 .. 2", self.nodes, DEBUG_MODE)

        # Network part 0-1
        #------------------
        # Node0 create CSW Tx with a new unused nullifier
        mark_logs("\nCreate a CSW withdrawing one sixth of the original sc balance... ", self.nodes, DEBUG_MODE)
        sc_csw_amount = sc_bal/6
        sc_csws[0]['amount'] = sc_csw_amount
        null_n0 = generate_random_field_element_hex()
        sc_csws[0]['nullifier'] = null_n0
        taddr_1 = self.nodes[1].getnewaddress()
        sc_csw_tx_outs_1 = {taddr_1: sc_csw_amount}

        try:
            rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs_1, sc_csws, [], [])
            funded_tx = self.nodes[0].fundrawtransaction(rawtx)
            sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
            tx_n1 = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send csw failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        sync_mempools(self.nodes[0:2])
        mark_logs("Node0 sent csw {} retrieving {} coins on Node1 behalf".format(tx_n1, sc_csws[0]['amount']), self.nodes, DEBUG_MODE)

        self.nodes[0].generate(1)
        sync_blocks(self.nodes[0:2])
        sync_mempools(self.nodes[0:2])

        n1_bal = self.nodes[1].z_gettotalbalance()['total']
        mark_logs("Node1 has {} confirmed balance".format(n1_bal), self.nodes, DEBUG_MODE)
        assert_equal(Decimal(n1_bal), Decimal(amount_2_1 + sc_bal/6)) 

        # Node1 sends all of its balance to Node0
        amount = Decimal(n1_bal) - Decimal("0.0001")
        tx_1_0 = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), amount);
        mark_logs("tx = {}".format(tx_1_0), self.nodes, DEBUG_MODE)
        mark_logs("Node1 sent {} coins to Node0 using csw funds...".format(amount), self.nodes, DEBUG_MODE)
        sync_mempools(self.nodes[0:2])

        mark_logs("Check tx is in mempool", self.nodes, DEBUG_MODE)
        assert_true(tx_1_0 in self.nodes[0].getrawmempool()) 
        assert_true(tx_1_0 in self.nodes[1].getrawmempool()) 

        mark_logs("Check tx is in Node0 wallet", self.nodes, DEBUG_MODE)
        utxos = self.nodes[0].listunspent(0, 0)
        assert_equal(tx_1_0, utxos[0]['txid']) 

        # check we have emptied the SC balance
        sc_bal_n0 = self.nodes[0].getscinfo(scid)['items'][0]['balance']
        mark_logs("sc balance from Node0 view is: {}".format(sc_bal_n0), self.nodes, DEBUG_MODE)
        assert_equal(Decimal(sc_bal_n0), Decimal('0.0'))

        print
        # Network part 2
        #------------------
        mark_logs("Create a CSW withdrawing one twelfth of the original sc balance... ", self.nodes, DEBUG_MODE)
        sc_csw_amount = sc_bal/12
        sc_csws[0]['amount'] = sc_csw_amount
        sc_csw_tx_outs = {taddr_2: sc_csw_amount}
        null_n2 = generate_random_field_element_hex()
        sc_csws[0]['nullifier'] = null_n2
        csw_mc_address = self.nodes[2].getnewaddress()
        sc_csws[0]['senderAddress'] = csw_mc_address
        try:
            rawtx = self.nodes[2].createrawtransaction([], sc_csw_tx_outs, sc_csws, [], [])
            funded_tx = self.nodes[2].fundrawtransaction(rawtx)
            sigRawtx = self.nodes[2].signrawtransaction(funded_tx['hex'])
            tx_n2 = self.nodes[2].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send csw failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        time.sleep(1)

        mark_logs("Node2 sent csw {} retrieving {} coins for himself".format(tx_n2, sc_csws[0]['amount']), self.nodes, DEBUG_MODE)

        self.nodes[2].generate(1)
        time.sleep(1)
        
        n2_bal = self.nodes[2].z_gettotalbalance()['total']
        mark_logs("Node2 has {} confirmed balance".format(n2_bal), self.nodes, DEBUG_MODE)

        # check we have still 1.0 coin in the the SC balance (12-6-4-1=1)
        sc_bal_n2 = self.nodes[2].getscinfo(scid)['items'][0]['balance']
        mark_logs("sc balance from Node2 view is: {}".format(sc_bal_n2), self.nodes, DEBUG_MODE)
        assert_equal(Decimal(sc_bal_n2), Decimal('1.0'))
        

        #============================================================================================
        mark_logs("\nJoining network", self.nodes, DEBUG_MODE)
        self.join_network()
        mark_logs("Network joined", self.nodes, DEBUG_MODE)

        self.nodes[2].generate(5)
        time.sleep(1)

        # Node2 should have prevailed therefore nullifier n0 should have disappeared
        mark_logs("Check nullifier used by Node0 is not in MC...", self.nodes, DEBUG_MODE)
        res = self.nodes[2].checkcswnullifier(scid, null_n0)
        assert_equal(res['data'], 'false')

        mark_logs("Check nullifier used by Node2 is in MC also from Node0 perspective...", self.nodes, DEBUG_MODE)
        res = self.nodes[0].checkcswnullifier(scid, null_n2)
        assert_equal(res['data'], 'true')

        mark_logs("Check tx {} is no more in mempool".format(tx_1_0), self.nodes, DEBUG_MODE)
        assert_false(tx_1_0 in self.nodes[0].getrawmempool()) 
        assert_false(tx_1_0 in self.nodes[1].getrawmempool()) 

        mark_logs("Check it is in no more in Node0 wallet too", self.nodes, DEBUG_MODE)
        utxos = self.nodes[0].listunspent(0)
        for x in utxos:
            if x['txid'] == tx_1_0:
                assert_true(False)

        n1_bal = self.nodes[1].z_gettotalbalance()['total']
        mark_logs("Node1 has {} confirmed balance".format(n1_bal), self.nodes, DEBUG_MODE)
        assert_equal(Decimal(n1_bal), Decimal(amount_2_1)) 

        mark_logs("Checking mempools...", self.nodes, DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            assert_equal(0, len(self.nodes[i].getrawmempool()))

        mark_logs("Checking sc balance...", self.nodes, DEBUG_MODE)
        sc_bal_n0 = self.nodes[0].getscinfo(scid)['items'][0]['balance']
        sc_bal_n1 = self.nodes[1].getscinfo(scid)['items'][0]['balance']
        sc_bal_n2 = self.nodes[2].getscinfo(scid)['items'][0]['balance']
        assert_equal(Decimal('1.0'), sc_bal_n0)
        assert_equal(sc_bal_n1, sc_bal_n0)
        assert_equal(sc_bal_n2, sc_bal_n0)

        n0_bal = self.nodes[0].z_gettotalbalance()['total']
        n1_bal = self.nodes[1].z_gettotalbalance()['total']
        n2_bal = self.nodes[2].z_gettotalbalance()['total']

        mark_logs("\nChecking persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        mark_logs("Checking nodes balance...", self.nodes, DEBUG_MODE)
        assert_equal(n0_bal, self.nodes[0].z_gettotalbalance()['total'])
        assert_equal(n1_bal, self.nodes[1].z_gettotalbalance()['total'])
        assert_equal(n2_bal, self.nodes[2].z_gettotalbalance()['total'])

        mark_logs("Checking sc balance...", self.nodes, DEBUG_MODE)
        assert_equal(sc_bal_n0, self.nodes[0].getscinfo(scid)['items'][0]['balance'])
        assert_equal(sc_bal_n1, self.nodes[1].getscinfo(scid)['items'][0]['balance'])
        assert_equal(sc_bal_n2, self.nodes[2].getscinfo(scid)['items'][0]['balance'])

        mark_logs("Checking nullifiers...", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[0].checkcswnullifier(scid, null1)['data'], 'true')
        assert_equal(self.nodes[0].checkcswnullifier(scid, null2)['data'], 'true')
        assert_equal(self.nodes[0].checkcswnullifier(scid, null_n0)['data'], 'false')
        assert_equal(self.nodes[0].checkcswnullifier(scid, null_n2)['data'], 'true')

        mark_logs("\nVerify we need at least 2 certificates for a CSW to be legal...", self.nodes, DEBUG_MODE)
        
        prev_epoch_hash = self.nodes[0].getbestblockhash()
        vk2 = mcTest.generate_params("sc2")
        cswVk2 = mcTest.generate_params("csw2")

        ret = self.nodes[0].sc_create(sc_epoch_len, "dada", sc_cr_amount, vk2, "abcdef", constant, cswVk2)
        creating_tx = ret['txid']
        mark_logs("Node 0 created SC spending {} coins via tx1 {}.".format(sc_cr_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()
        scid2 = self.nodes[0].getrawtransaction(creating_tx, 1)['vsc_ccout'][0]['scid']
        mark_logs("==> created SC ids {}".format(scid2), self.nodes, DEBUG_MODE)
            
        # advance just one epoch and cease it
        mark_logs("\nLet 1 epochs pass by...".  format(sc_epoch_len), self.nodes, DEBUG_MODE)

        cert, epoch_block_hash, epoch_number = advance_epoch(
            mcTest, self.nodes[0], self.sync_all,
             scid2, prev_epoch_hash, "sc2", constant, sc_epoch_len)

        mark_logs("\n==> certificate for epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        mark_logs("Let SC cease... ".format(scid2), self.nodes, DEBUG_MODE)

        nbl = int(sc_epoch_len * 1.5)
        mark_logs("Node0 generates {} blocks".format(nbl), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(nbl)
        self.sync_all()

        # check it is really ceased
        ret = self.nodes[0].getscinfo(scid2, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")

        # and has the expected balance
        sc_bal = self.nodes[0].getscinfo(scid2, False, False)['items'][0]['balance']
        assert_equal(sc_bal, sc_cr_amount)

        # Try to create a CSW with less than minimal number of certificates (min is 2)
        mark_logs("\nTrying to send a csw with just one cert (expecting failure...)", self.nodes, DEBUG_MODE)
        csw_mc_address = self.nodes[0].getnewaddress()
        sc_csw_amount = sc_bal
        null3 = generate_random_field_element_hex()

        sc_csws = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid2,
            "epoch": 0,
            "nullifier": null2,
            # Temp hardcoded proof with valid structure TODO: generate a real valid CSW proof
            "scProof": "927e725a39f1c219a458f02d27fb327cc9595985ed947553d979261261b96360b23633b747df8141bcb12076b75f654c35ba0869df74a236763fe0c070e6da2959c1a8c77330783e76e4ad5801818c5edb06567196813355bea5e08beaa5010088965b13b48cbf962106500727ba05b31b4f429076230a90384d18b0e5f395a87ea466704a56375d3a68e65777568881b432208029c12cda5d089f596cf91da14392ed6c619c195a6bebe04c2caba17443906fbf386bc4555b0b721a1ead0000007acd59b470379a38d8de9e82b54fdd1e4e8bd8b2059b62552814989c25f7e07c6261ebc6de8b4b875893a874df953594beb119d53fd74e33e09cb66ed717c393c3fd22f1b465332a17c3d934172fdd33d1c641a9121c5e762b6e59305d1d0100ec5aed56c4290c6bb57e1d1b5b2b1f861f9926403446482f72cede346c0feae2817a2f18b7a37a9b55a3e9deb2a555ffb0d9331cb320ce18aa99a2c2c025c3d28afc77c631263b91160b1f556a6d1d158a8d3c56ab61dc9396e6536094720000741ae2c1569b098231dce089680fb1e561d974ce4f4e00cbe1150281ce12dd561be12a7fefcb30f62d3c8934926ae4eb4a4cb4378dd2568648ff12a7c36302be4d5a578dc360a3125b0c1427fb6b55a067f01d24d616c954bce363a8ef1001003a1ebe119da0561bf1d3294819759677fbd37dbac403662e263bfa71a4992228557a31d2d9ce0a7ffcbe91aa57f38cae7b51ef2681b16f275c0f87c89fbc2060690ac77dc1d3d20b7d3c6b5af1c92ee96e61b6635e343c3976112eb4ec91000000fe60207ddf86be08604c41f46f2e3740b479cad9fd1cb5f8c589595ba3d50f6c3984bbe707d460a0e27d4ec90d89a3476c647a6ea262b910dcb267325c375c713ff7031fe3a200130060bf09900e2e5244f88355a2a0587b068caae7f65b01005f0fb082380604a78c66e21681c2c7f3f59042c7b4495435b8d972bbb535ae8dd09ea8232b0161dc3a13f4a718b5a7fa4cb01d6625e38d73032baf3a9ffcff5a7493a27eeab25c97bee8eddf2fd2c9e9dd1bd1813c22b046c01caccc7478000000"
        }]

        out_amount = sc_csw_amount / Decimal("2.0")
        sc_csw_tx_outs = {taddr_2: out_amount}
        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws, [], [])
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
        try:
            tx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
            mark_logs("===== Should never get here: TODO import fix from certHashData_refactoring branch".format(errorString), self.nodes, DEBUG_MODE)
            pprint.pprint(self.nodes[0].getrawtransaction(tx, 1))
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send csw failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)


        # check we have all cert data hash for both sc ids
        mark_logs("\nCheck we have expected cert data hashes for both sidechains", self.nodes, DEBUG_MODE)
        try:
            assert_true(self.nodes[1].getactivecertdatahash(scid)['certDataHash'])

            assert_true(self.nodes[1].getactivecertdatahash(scid2)['certDataHash'])
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("{}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)


if __name__ == '__main__':
    CswNullifierTest().main()
