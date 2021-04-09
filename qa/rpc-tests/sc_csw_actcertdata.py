#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

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
class CswActCertDataTest(BitcoinTestFramework):

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
        Create two SCs, advance two epochs and then let them cease.
        Test some CSW txes, verifying that active cert data is correctly handled
        Restart the network and check DB integrity.
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
        vk1 = mcTest.generate_params("sc1")
        vk2 = mcTest.generate_params("sc2")
        cswVk1 = mcTest.generate_params("csw1")
        cswVk2 = mcTest.generate_params("csw2")
        constant1 = generate_random_field_element_hex()
        constant2 = generate_random_field_element_hex()

        sc_cr = []
        sc_cr.append({
            "epoch_length": sc_epoch_len,
            "amount": sc_cr_amount,
            "address": sc_address,
            "wCertVk": vk1,
            "wCeasedVk": cswVk1,
            "constant": constant1
        })

        sc_cr.append({
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

        cert, epoch_block_hash, epoch_number = advance_epoch(
            mcTest, self.nodes[0], self.sync_all,
            scid1, prev_epoch_hash, "sc1", constant1, sc_epoch_len)

        mark_logs("\n==> certificate for SC1 epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        cert, epoch_block_hash, epoch_number = advance_epoch(
            mcTest, self.nodes[0], self.sync_all,
            scid2, prev_epoch_hash, "sc2", constant2, sc_epoch_len, generate=False) # do not generate

        mark_logs("\n==> certificate for SC2 epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        prev_epoch_hash = epoch_block_hash

        cert, epoch_block_hash, epoch_number = advance_epoch(
            mcTest, self.nodes[0], self.sync_all,
             scid1, prev_epoch_hash, "sc1", constant1, sc_epoch_len)

        mark_logs("\n==> certificate for SC1 epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        cert, epoch_block_hash, epoch_number = advance_epoch(
            mcTest, self.nodes[0], self.sync_all,
             scid2, prev_epoch_hash, "sc2", constant2, sc_epoch_len, generate=False) # do not generate

        mark_logs("\n==> certificate for SC2 epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        prev_epoch_hash = epoch_block_hash

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

        sc_csws = [
        {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid1,
            "epoch": 0,
            "nullifier": null_1_1,
            "activeCertData": actCertData1,
            # Temp hardcoded proof with valid structure TODO: generate a real valid CSW proof
            "scProof": "927e725a39f1c219a458f02d27fb327cc9595985ed947553d979261261b96360b23633b747df8141bcb12076b75f654c35ba0869df74a236763fe0c070e6da2959c1a8c77330783e76e4ad5801818c5edb06567196813355bea5e08beaa5010088965b13b48cbf962106500727ba05b31b4f429076230a90384d18b0e5f395a87ea466704a56375d3a68e65777568881b432208029c12cda5d089f596cf91da14392ed6c619c195a6bebe04c2caba17443906fbf386bc4555b0b721a1ead0000007acd59b470379a38d8de9e82b54fdd1e4e8bd8b2059b62552814989c25f7e07c6261ebc6de8b4b875893a874df953594beb119d53fd74e33e09cb66ed717c393c3fd22f1b465332a17c3d934172fdd33d1c641a9121c5e762b6e59305d1d0100ec5aed56c4290c6bb57e1d1b5b2b1f861f9926403446482f72cede346c0feae2817a2f18b7a37a9b55a3e9deb2a555ffb0d9331cb320ce18aa99a2c2c025c3d28afc77c631263b91160b1f556a6d1d158a8d3c56ab61dc9396e6536094720000741ae2c1569b098231dce089680fb1e561d974ce4f4e00cbe1150281ce12dd561be12a7fefcb30f62d3c8934926ae4eb4a4cb4378dd2568648ff12a7c36302be4d5a578dc360a3125b0c1427fb6b55a067f01d24d616c954bce363a8ef1001003a1ebe119da0561bf1d3294819759677fbd37dbac403662e263bfa71a4992228557a31d2d9ce0a7ffcbe91aa57f38cae7b51ef2681b16f275c0f87c89fbc2060690ac77dc1d3d20b7d3c6b5af1c92ee96e61b6635e343c3976112eb4ec91000000fe60207ddf86be08604c41f46f2e3740b479cad9fd1cb5f8c589595ba3d50f6c3984bbe707d460a0e27d4ec90d89a3476c647a6ea262b910dcb267325c375c713ff7031fe3a200130060bf09900e2e5244f88355a2a0587b068caae7f65b01005f0fb082380604a78c66e21681c2c7f3f59042c7b4495435b8d972bbb535ae8dd09ea8232b0161dc3a13f4a718b5a7fa4cb01d6625e38d73032baf3a9ffcff5a7493a27eeab25c97bee8eddf2fd2c9e9dd1bd1813c22b046c01caccc7478000000"
        },
        {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid1,
            "epoch": 0,
            "nullifier": null_1_2,
            "activeCertData": actCertData1,
            # Temp hardcoded proof with valid structure TODO: generate a real valid CSW proof
            "scProof": "927e725a39f1c219a458f02d27fb327cc9595985ed947553d979261261b96360b23633b747df8141bcb12076b75f654c35ba0869df74a236763fe0c070e6da2959c1a8c77330783e76e4ad5801818c5edb06567196813355bea5e08beaa5010088965b13b48cbf962106500727ba05b31b4f429076230a90384d18b0e5f395a87ea466704a56375d3a68e65777568881b432208029c12cda5d089f596cf91da14392ed6c619c195a6bebe04c2caba17443906fbf386bc4555b0b721a1ead0000007acd59b470379a38d8de9e82b54fdd1e4e8bd8b2059b62552814989c25f7e07c6261ebc6de8b4b875893a874df953594beb119d53fd74e33e09cb66ed717c393c3fd22f1b465332a17c3d934172fdd33d1c641a9121c5e762b6e59305d1d0100ec5aed56c4290c6bb57e1d1b5b2b1f861f9926403446482f72cede346c0feae2817a2f18b7a37a9b55a3e9deb2a555ffb0d9331cb320ce18aa99a2c2c025c3d28afc77c631263b91160b1f556a6d1d158a8d3c56ab61dc9396e6536094720000741ae2c1569b098231dce089680fb1e561d974ce4f4e00cbe1150281ce12dd561be12a7fefcb30f62d3c8934926ae4eb4a4cb4378dd2568648ff12a7c36302be4d5a578dc360a3125b0c1427fb6b55a067f01d24d616c954bce363a8ef1001003a1ebe119da0561bf1d3294819759677fbd37dbac403662e263bfa71a4992228557a31d2d9ce0a7ffcbe91aa57f38cae7b51ef2681b16f275c0f87c89fbc2060690ac77dc1d3d20b7d3c6b5af1c92ee96e61b6635e343c3976112eb4ec91000000fe60207ddf86be08604c41f46f2e3740b479cad9fd1cb5f8c589595ba3d50f6c3984bbe707d460a0e27d4ec90d89a3476c647a6ea262b910dcb267325c375c713ff7031fe3a200130060bf09900e2e5244f88355a2a0587b068caae7f65b01005f0fb082380604a78c66e21681c2c7f3f59042c7b4495435b8d972bbb535ae8dd09ea8232b0161dc3a13f4a718b5a7fa4cb01d6625e38d73032baf3a9ffcff5a7493a27eeab25c97bee8eddf2fd2c9e9dd1bd1813c22b046c01caccc7478000000"
        },
        {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid1,
            "epoch": 0,
            "nullifier": null_1_3,
            "activeCertData": actCertData1,
            # Temp hardcoded proof with valid structure TODO: generate a real valid CSW proof
            "scProof": "927e725a39f1c219a458f02d27fb327cc9595985ed947553d979261261b96360b23633b747df8141bcb12076b75f654c35ba0869df74a236763fe0c070e6da2959c1a8c77330783e76e4ad5801818c5edb06567196813355bea5e08beaa5010088965b13b48cbf962106500727ba05b31b4f429076230a90384d18b0e5f395a87ea466704a56375d3a68e65777568881b432208029c12cda5d089f596cf91da14392ed6c619c195a6bebe04c2caba17443906fbf386bc4555b0b721a1ead0000007acd59b470379a38d8de9e82b54fdd1e4e8bd8b2059b62552814989c25f7e07c6261ebc6de8b4b875893a874df953594beb119d53fd74e33e09cb66ed717c393c3fd22f1b465332a17c3d934172fdd33d1c641a9121c5e762b6e59305d1d0100ec5aed56c4290c6bb57e1d1b5b2b1f861f9926403446482f72cede346c0feae2817a2f18b7a37a9b55a3e9deb2a555ffb0d9331cb320ce18aa99a2c2c025c3d28afc77c631263b91160b1f556a6d1d158a8d3c56ab61dc9396e6536094720000741ae2c1569b098231dce089680fb1e561d974ce4f4e00cbe1150281ce12dd561be12a7fefcb30f62d3c8934926ae4eb4a4cb4378dd2568648ff12a7c36302be4d5a578dc360a3125b0c1427fb6b55a067f01d24d616c954bce363a8ef1001003a1ebe119da0561bf1d3294819759677fbd37dbac403662e263bfa71a4992228557a31d2d9ce0a7ffcbe91aa57f38cae7b51ef2681b16f275c0f87c89fbc2060690ac77dc1d3d20b7d3c6b5af1c92ee96e61b6635e343c3976112eb4ec91000000fe60207ddf86be08604c41f46f2e3740b479cad9fd1cb5f8c589595ba3d50f6c3984bbe707d460a0e27d4ec90d89a3476c647a6ea262b910dcb267325c375c713ff7031fe3a200130060bf09900e2e5244f88355a2a0587b068caae7f65b01005f0fb082380604a78c66e21681c2c7f3f59042c7b4495435b8d972bbb535ae8dd09ea8232b0161dc3a13f4a718b5a7fa4cb01d6625e38d73032baf3a9ffcff5a7493a27eeab25c97bee8eddf2fd2c9e9dd1bd1813c22b046c01caccc7478000000"
        },
        {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid2,
            "epoch": 0,
            "nullifier": null_2_1,
            "activeCertData": actCertData2,
            # Temp hardcoded proof with valid structure TODO: generate a real valid CSW proof
            "scProof": "927e725a39f1c219a458f02d27fb327cc9595985ed947553d979261261b96360b23633b747df8141bcb12076b75f654c35ba0869df74a236763fe0c070e6da2959c1a8c77330783e76e4ad5801818c5edb06567196813355bea5e08beaa5010088965b13b48cbf962106500727ba05b31b4f429076230a90384d18b0e5f395a87ea466704a56375d3a68e65777568881b432208029c12cda5d089f596cf91da14392ed6c619c195a6bebe04c2caba17443906fbf386bc4555b0b721a1ead0000007acd59b470379a38d8de9e82b54fdd1e4e8bd8b2059b62552814989c25f7e07c6261ebc6de8b4b875893a874df953594beb119d53fd74e33e09cb66ed717c393c3fd22f1b465332a17c3d934172fdd33d1c641a9121c5e762b6e59305d1d0100ec5aed56c4290c6bb57e1d1b5b2b1f861f9926403446482f72cede346c0feae2817a2f18b7a37a9b55a3e9deb2a555ffb0d9331cb320ce18aa99a2c2c025c3d28afc77c631263b91160b1f556a6d1d158a8d3c56ab61dc9396e6536094720000741ae2c1569b098231dce089680fb1e561d974ce4f4e00cbe1150281ce12dd561be12a7fefcb30f62d3c8934926ae4eb4a4cb4378dd2568648ff12a7c36302be4d5a578dc360a3125b0c1427fb6b55a067f01d24d616c954bce363a8ef1001003a1ebe119da0561bf1d3294819759677fbd37dbac403662e263bfa71a4992228557a31d2d9ce0a7ffcbe91aa57f38cae7b51ef2681b16f275c0f87c89fbc2060690ac77dc1d3d20b7d3c6b5af1c92ee96e61b6635e343c3976112eb4ec91000000fe60207ddf86be08604c41f46f2e3740b479cad9fd1cb5f8c589595ba3d50f6c3984bbe707d460a0e27d4ec90d89a3476c647a6ea262b910dcb267325c375c713ff7031fe3a200130060bf09900e2e5244f88355a2a0587b068caae7f65b01005f0fb082380604a78c66e21681c2c7f3f59042c7b4495435b8d972bbb535ae8dd09ea8232b0161dc3a13f4a718b5a7fa4cb01d6625e38d73032baf3a9ffcff5a7493a27eeab25c97bee8eddf2fd2c9e9dd1bd1813c22b046c01caccc7478000000"
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
        # vact_cert_data - size(2): one for each sc
        assert_equal(1, len(decoded_tx['vin']))
        assert_equal(2, len(decoded_tx['vout']))
        assert_equal(4, len(decoded_tx['vcsw_ccin']))
        assert_equal(2, len(decoded_tx['vact_cert_data']))

        # all csw input for SC1 point to the first act cert data
        for i in range(0, 3):
            assert_equal(decoded_tx['vcsw_ccin'][i]['actCertDataIdx'], 0)

        # csw input for SC2 points to the second act cert data
        assert_equal(decoded_tx['vcsw_ccin'][3]['actCertDataIdx'], 1)

        assert_equal(decoded_tx['vact_cert_data'][0], str(actCertData1))
        assert_equal(decoded_tx['vact_cert_data'][1], str(actCertData2))

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

        sc_csws = [ {
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid1,
            "epoch": 0,
            "nullifier": null_1_4,
            "activeCertData": generate_random_field_element_hex(),
            # Temp hardcoded proof with valid structure TODO: generate a real valid CSW proof
            "scProof": "927e725a39f1c219a458f02d27fb327cc9595985ed947553d979261261b96360b23633b747df8141bcb12076b75f654c35ba0869df74a236763fe0c070e6da2959c1a8c77330783e76e4ad5801818c5edb06567196813355bea5e08beaa5010088965b13b48cbf962106500727ba05b31b4f429076230a90384d18b0e5f395a87ea466704a56375d3a68e65777568881b432208029c12cda5d089f596cf91da14392ed6c619c195a6bebe04c2caba17443906fbf386bc4555b0b721a1ead0000007acd59b470379a38d8de9e82b54fdd1e4e8bd8b2059b62552814989c25f7e07c6261ebc6de8b4b875893a874df953594beb119d53fd74e33e09cb66ed717c393c3fd22f1b465332a17c3d934172fdd33d1c641a9121c5e762b6e59305d1d0100ec5aed56c4290c6bb57e1d1b5b2b1f861f9926403446482f72cede346c0feae2817a2f18b7a37a9b55a3e9deb2a555ffb0d9331cb320ce18aa99a2c2c025c3d28afc77c631263b91160b1f556a6d1d158a8d3c56ab61dc9396e6536094720000741ae2c1569b098231dce089680fb1e561d974ce4f4e00cbe1150281ce12dd561be12a7fefcb30f62d3c8934926ae4eb4a4cb4378dd2568648ff12a7c36302be4d5a578dc360a3125b0c1427fb6b55a067f01d24d616c954bce363a8ef1001003a1ebe119da0561bf1d3294819759677fbd37dbac403662e263bfa71a4992228557a31d2d9ce0a7ffcbe91aa57f38cae7b51ef2681b16f275c0f87c89fbc2060690ac77dc1d3d20b7d3c6b5af1c92ee96e61b6635e343c3976112eb4ec91000000fe60207ddf86be08604c41f46f2e3740b479cad9fd1cb5f8c589595ba3d50f6c3984bbe707d460a0e27d4ec90d89a3476c647a6ea262b910dcb267325c375c713ff7031fe3a200130060bf09900e2e5244f88355a2a0587b068caae7f65b01005f0fb082380604a78c66e21681c2c7f3f59042c7b4495435b8d972bbb535ae8dd09ea8232b0161dc3a13f4a718b5a7fa4cb01d6625e38d73032baf3a9ffcff5a7493a27eeab25c97bee8eddf2fd2c9e9dd1bd1813c22b046c01caccc7478000000"
        } ]

        # recipient MC address
        sc_csw_tx_outs = {taddr_2: Decimal(sc_csw_amount)}

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        try:
            self.nodes[0].sendrawtransaction(sigRawtx['hex'])
            assert(False)
        except JSONRPCException, e:
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
