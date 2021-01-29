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
    start_nodes, connect_nodes_bi, assert_true, assert_false
from test_framework.mc_test.mc_test import *

from decimal import Decimal

NUMB_OF_NODES = 3


# Create one-input, one-output, no-fee transaction:
class CswNullifierTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[["-sccoinsmaturity=0", '-logtimemicros=1', '-debug=sc', '-debug=py',
                                              '-debug=mempool', '-debug=net', '-debug=bench']] * NUMB_OF_NODES)

        # connect to a local machine for debugging
        # url = "http://bitcoinrpc:DP6DvqZtqXarpeNWyN3LZTFchCCyCUuHwNF7E8pX99x1@%s:%d" % ('127.0.0.1', 18232)
        # proxy = AuthServiceProxy(url)
        # proxy.url = url # store URL on proxy for info
        # self.nodes.append(proxy)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 0, 2)

        self.is_network_split = False
        self.sync_all()

    def print_data(self, index):
        print("////////////////////")
        walletinfo = self.nodes[index].getwalletinfo()
        print("Nodo: ", index, " Wallet-balance: ", walletinfo['balance'])
        print("Nodo: ", index, " Wallet-immature_balance: ", walletinfo['immature_balance'])
        print("Nodo: ", index, " z_total_balance: ", self.nodes[index].z_gettotalbalance())

    print("////////////////////")

    def run_test(self):

        # prepare some coins for multiple *rawtransaction csw commands
        self.nodes[2].generate(1)
        self.sync_all()
        self.nodes[0].generate(101)
        self.sync_all()
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 1.5);
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 1.0);
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 5.0);
        self.sync_all()
        self.nodes[0].generate(5)
        self.sync_all()

        print("Node 1 generate " + str(220 - self.nodes[0].getblockcount() + 1) + " blocks to reach height 221...")
        # reach block height 221 needed to create a SC
        self.nodes[1].generate(220 - self.nodes[0].getblockcount() + 1)
        self.sync_all()

        sc_address = "0000000000000000000000000000000000000000000000000000000000000abc"
        sc_epoch = 123
        sc_epoch2 = 1000
        sc_cr_amount = Decimal('10.00000000')
        sc_cr_amount2 = Decimal('0.100000000')

        print("Node 1 sends 10 coins to node 0 to have a UTXO...")
        txid = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), sc_cr_amount + sc_cr_amount2)
        self.sync_all()

        self.nodes[0].generate(1)
        self.sync_all()

        # generate wCertVk and constant
        mcTest = MCTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        cswVk = mcTest.generate_params("csw1")
        constant = generate_random_field_element_hex()

        sc_cr = []
        sc_cr.append({
            "epoch_length": sc_epoch,
            "amount": sc_cr_amount,
            "address": sc_address,
            "wCertVk": vk,
            "wCeasedVk": cswVk,
            "constant": constant
        })

        sc_cr.append({
            "epoch_length": sc_epoch2,
            "amount": sc_cr_amount2,
            "address": sc_address,
            "wCertVk": vk,
            "wCeasedVk": cswVk,
            "constant": constant
        })

        # Create a SC
        print("Create SC")

        decoded_tx = self.nodes[0].decoderawtransaction(self.nodes[0].gettransaction(txid)['hex'])
        vout = {}
        for outpoint in decoded_tx['vout']:
            if outpoint['value'] == sc_cr_amount + sc_cr_amount2:
                vout = outpoint
                break;

        inputs = [{'txid': txid, 'vout': vout['n']}]
        rawtx = self.nodes[0].createrawtransaction(inputs, {}, [], sc_cr, [])
        sigRawtx = self.nodes[0].signrawtransaction(rawtx)
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])

        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)
        scid = decoded_tx['vsc_ccout'][0]['scid']

        self.nodes[0].generate(1)
        self.sync_all()

        print("Verify all nodes see the new SC...")
        scinfo0 = self.nodes[0].getscinfo(scid)['items'][0]
        scinfo1 = self.nodes[1].getscinfo(scid)['items'][0]
        scinfo2 = self.nodes[2].getscinfo(scid)['items'][0]
        assert_equal(scinfo0, scinfo1)
        assert_equal(scinfo0, scinfo2)

        # Try create a CSW input
        print("Try create new CSW from the SC just created")

        print("Making SC ceased")
        self.nodes[0].generate(int(sc_epoch * 1.25))
        self.sync_all()

        csw_mc_address = self.nodes[0].getnewaddress()

        sc_ft_amount = Decimal('10.00000000')
        sc_csw_amount = sc_ft_amount / 5

        sc_csws = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": generate_random_field_element_hex(),
            # Temp hardcoded proof with valid structure
            # TODO: generate a real valid CSW proof
            "scProof": "927e725a39f1c219a458f02d27fb327cc9595985ed947553d979261261b96360b23633b747df8141bcb12076b75f654c35ba0869df74a236763fe0c070e6da2959c1a8c77330783e76e4ad5801818c5edb06567196813355bea5e08beaa5010088965b13b48cbf962106500727ba05b31b4f429076230a90384d18b0e5f395a87ea466704a56375d3a68e65777568881b432208029c12cda5d089f596cf91da14392ed6c619c195a6bebe04c2caba17443906fbf386bc4555b0b721a1ead0000007acd59b470379a38d8de9e82b54fdd1e4e8bd8b2059b62552814989c25f7e07c6261ebc6de8b4b875893a874df953594beb119d53fd74e33e09cb66ed717c393c3fd22f1b465332a17c3d934172fdd33d1c641a9121c5e762b6e59305d1d0100ec5aed56c4290c6bb57e1d1b5b2b1f861f9926403446482f72cede346c0feae2817a2f18b7a37a9b55a3e9deb2a555ffb0d9331cb320ce18aa99a2c2c025c3d28afc77c631263b91160b1f556a6d1d158a8d3c56ab61dc9396e6536094720000741ae2c1569b098231dce089680fb1e561d974ce4f4e00cbe1150281ce12dd561be12a7fefcb30f62d3c8934926ae4eb4a4cb4378dd2568648ff12a7c36302be4d5a578dc360a3125b0c1427fb6b55a067f01d24d616c954bce363a8ef1001003a1ebe119da0561bf1d3294819759677fbd37dbac403662e263bfa71a4992228557a31d2d9ce0a7ffcbe91aa57f38cae7b51ef2681b16f275c0f87c89fbc2060690ac77dc1d3d20b7d3c6b5af1c92ee96e61b6635e343c3976112eb4ec91000000fe60207ddf86be08604c41f46f2e3740b479cad9fd1cb5f8c589595ba3d50f6c3984bbe707d460a0e27d4ec90d89a3476c647a6ea262b910dcb267325c375c713ff7031fe3a200130060bf09900e2e5244f88355a2a0587b068caae7f65b01005f0fb082380604a78c66e21681c2c7f3f59042c7b4495435b8d972bbb535ae8dd09ea8232b0161dc3a13f4a718b5a7fa4cb01d6625e38d73032baf3a9ffcff5a7493a27eeab25c97bee8eddf2fd2c9e9dd1bd1813c22b046c01caccc7478000000"
        }]

        sc_csw_tx_outs = {self.nodes[0].getnewaddress(): sc_csw_amount - Decimal('0.00001000')}

        # # Create Tx with single CSW input and single output. CSW input signed as "NONE"
        print("Create Tx with a single CSW input and single output. CSW input signed as 'NONE'.")
        sc_csws[0]['nullifier'] = generate_random_field_element_hex()

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws, [], [])
        sigRawtx = self.nodes[0].signrawtransaction(rawtx, None, None, "NONE")
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])

        self.sync_all()

        print("Check the getrawtransaction result for Tx with CSW input signed as 'NONE'.")
        self.nodes[1].getrawtransaction(finalRawtx, 1)

        self.nodes[0].generate(1)
        self.sync_all()

        # Try to create CSW with the nullifier which already exists in the chain
        print("Check the getrawtransaction result for Tx with existed nullifier is failed.")
        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws, [], [])
        sigRawtx = self.nodes[0].signrawtransaction(rawtx)

        error_occurred = False
        try:
            self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException, e:
            error_occurred = True

        assert_true(error_occurred, "CSW with used nullifer. Expected to be rejected by the mempool.")

        print("Check presence of the nullifier in MC.")
        res = self.nodes[0].checkcswnullifier(scid, sc_csws[0]['nullifier'])
        assert(res['data'], 'true')

        # Create Tx with unique nullifier
        print("Check the getrawtransaction result for Tx with unique nullifier is working.")
        sc_csws[0]['nullifier'] = generate_random_field_element_hex()
        print("Check uniqueness of the nullifier.")
        res = self.nodes[0].checkcswnullifier(scid, sc_csws[0]['nullifier'])
        assert(res['data'], 'false')
        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws, [], [])
        sigRawtx = self.nodes[0].signrawtransaction(rawtx)
        self.nodes[0].sendrawtransaction(sigRawtx['hex'])

        self.nodes[0].generate(1)
        self.sync_all()


if __name__ == '__main__':
    CswNullifierTest().main()
