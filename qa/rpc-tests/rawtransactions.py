#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test re-org scenarios with a mempool that contains transactions
# that spend (directly or indirectly) coinbase transactions.
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, connect_nodes_bi, assert_true
from test_framework.mc_test.mc_test import *

from decimal import Decimal

NUMB_OF_NODES=3

# Create one-input, one-output, no-fee transaction:
class RawTransactionsTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[["-sccoinsmaturity=0", '-logtimemicros=1', '-debug=sc', '-debug=py', '-debug=mempool', '-debug=net', '-debug=bench']] * NUMB_OF_NODES )

        #connect to a local machine for debugging
        #url = "http://bitcoinrpc:DP6DvqZtqXarpeNWyN3LZTFchCCyCUuHwNF7E8pX99x1@%s:%d" % ('127.0.0.1', 18232)
        #proxy = AuthServiceProxy(url)
        #proxy.url = url # store URL on proxy for info
        #self.nodes.append(proxy)

        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)

        self.is_network_split=False
        self.sync_all()

    def print_data(self,index):
        print("////////////////////")
        walletinfo=self.nodes[index].getwalletinfo()
        print("Nodo: ",index, " Wallet-balance: ",walletinfo['balance'])
        print("Nodo: ",index, " Wallet-immature_balance: ",walletinfo['immature_balance'])
        print("Nodo: ",index, " z_total_balance: ",self.nodes[index].z_gettotalbalance())

    print("////////////////////")

    def run_test(self):

        #prepare some coins for multiple *rawtransaction commands
        self.nodes[2].generate(1)
        self.sync_all()
        self.nodes[0].generate(101)
        self.sync_all()
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(),1.5);
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(),1.0);
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(),5.0);
        self.sync_all()
        self.nodes[0].generate(5)
        self.sync_all()

        #########################################
        # sendrawtransaction with missing input #
        #########################################
        inputs  = [ {'txid' : "1d1d4e24ed99057e84c3f80fd8fbec79ed9e1acee37da269356ecea000000000", 'vout' : 1}] #won't exists
        outputs = { self.nodes[0].getnewaddress() : 4.998 }
        rawtx   = self.nodes[2].createrawtransaction(inputs, outputs)
        rawtx   = self.nodes[2].signrawtransaction(rawtx)

        errorString = ""
        try:
            rawtx   = self.nodes[2].sendrawtransaction(rawtx['hex'])
        except JSONRPCException,e:
            errorString = e.error['message']

        assert_equal("Missing inputs" in errorString, True);

        #########################
        # RAW TX MULTISIG TESTS #
        #########################
        # 2of2 test
        addr1 = self.nodes[2].getnewaddress()
        addr2 = self.nodes[2].getnewaddress()

        addr1Obj = self.nodes[2].validateaddress(addr1)
        addr2Obj = self.nodes[2].validateaddress(addr2)

        mSigObj = self.nodes[2].addmultisigaddress(2, [addr1Obj['pubkey'], addr2Obj['pubkey']])
        mSigObjValid = self.nodes[2].validateaddress(mSigObj)

        #use balance deltas instead of absolute values
        bal = self.nodes[2].getbalance()

        # send 1.2 BTC to msig adr
        txId       = self.nodes[0].sendtoaddress(mSigObj, 1.2);
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()
        assert_equal(self.nodes[2].getbalance(), bal+Decimal('1.20000000')) #node2 has both keys of the 2of2 ms addr., tx should affect the balance

        # 2of3 test from different nodes
        bal = self.nodes[2].getbalance()
        addr1 = self.nodes[1].getnewaddress()
        addr2 = self.nodes[2].getnewaddress()
        addr3 = self.nodes[2].getnewaddress()

        addr1Obj = self.nodes[1].validateaddress(addr1)
        addr2Obj = self.nodes[2].validateaddress(addr2)
        addr3Obj = self.nodes[2].validateaddress(addr3)

        mSigObj = self.nodes[2].addmultisigaddress(2, [addr1Obj['pubkey'], addr2Obj['pubkey'], addr3Obj['pubkey']])
        mSigObjValid = self.nodes[2].validateaddress(mSigObj)
        assert_equal(mSigObjValid['isvalid'], True)

        txId       = self.nodes[0].sendtoaddress(mSigObj, 2.2);
        decTx = self.nodes[0].gettransaction(txId)
        rawTx = self.nodes[0].decoderawtransaction(decTx['hex'])
        sPK = rawTx['vout'][0]['scriptPubKey']['hex']
        [sPK] # hush pyflakes
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        # THIS IS A INCOMPLETE FEATURE
        # NODE2 HAS TWO OF THREE KEY AND THE FUNDS SHOULD BE SPENDABLE AND COUNT AT BALANCE CALCULATION
        assert_equal(self.nodes[2].getbalance(), bal) # for now, assume the funds of a 2of3 multisig tx are not marked as spendable

        txDetails = self.nodes[0].gettransaction(txId, True)
        rawTx = self.nodes[0].decoderawtransaction(txDetails['hex'])
        vout = False
        for outpoint in rawTx['vout']:
            if outpoint['value'] == Decimal('2.20000000'):
                vout = outpoint
                break;

        bal = self.nodes[0].getbalance()
        inputs = [{ "txid" : txId, "vout" : vout['n'], "scriptPubKey" : vout['scriptPubKey']['hex']}]
        outputs = { self.nodes[0].getnewaddress() : 2.199 }
        rawTx = self.nodes[2].createrawtransaction(inputs, outputs)
        rawTxPartialSigned = self.nodes[1].signrawtransaction(rawTx, inputs)
        assert_equal(rawTxPartialSigned['complete'], False) # node1 only has one key, can't comp. sign the tx

        rawTxSigned = self.nodes[2].signrawtransaction(rawTx, inputs)
        assert_equal(rawTxSigned['complete'], True) # node2 can sign the tx compl., own two of three keys
        self.nodes[2].sendrawtransaction(rawTxSigned['hex'])
        self.nodes[0].decoderawtransaction(rawTxSigned['hex'])
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()
        assert_equal(self.nodes[0].getbalance(), bal+Decimal('11.4375')+Decimal('2.19900000')) #block reward + tx

        #########################
        # RAW TX CREATE SC TEST #
        #########################
        print("Testing the SC creation with createrawtransaction function")

        print("Node 1 generate "+str(220-self.nodes[0].getblockcount()+1)+" blocks to reach height 221...")
        #reach block height 221 needed to create a SC
        self.nodes[1].generate(220-self.nodes[0].getblockcount()+1)
        self.sync_all()

        sc_address = "0000000000000000000000000000000000000000000000000000000000000abc"
        sc_epoch = 123
        sc_epoch2 = 1000
        sc_cr_amount = Decimal('10.00000000')
        sc_cr_amount2 = Decimal('0.100000000')

        print("Node 1 sends 10 coins to node 0 to have a UTXO...")
        txid=self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), sc_cr_amount + sc_cr_amount2)
        self.sync_all()

        self.nodes[0].generate(1)
        self.sync_all()

        #generate wCertVk and constant
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


        #Try create a SC with no inputs
        print("Try create a SC with no inputs...")

        rawtx=self.nodes[0].createrawtransaction([],{},[],sc_cr)
        sigRawtx = self.nodes[0].signrawtransaction(rawtx)
        try:
            finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException,e:
            errorString = e.error['message']
        assert_equal("vin-empty" in errorString, True)

        #Create a SC
        print("Create two SCs")

        decoded_tx=self.nodes[0].decoderawtransaction(self.nodes[0].gettransaction(txid)['hex'])
        vout = {}
        for outpoint in decoded_tx['vout']:
            if outpoint['value'] == sc_cr_amount + sc_cr_amount2:
                vout = outpoint
                break;

        inputs = [{'txid': txid, 'vout': vout['n']}]
        rawtx=self.nodes[0].createrawtransaction(inputs, {}, [], sc_cr, [])
        sigRawtx = self.nodes[0].signrawtransaction(rawtx)
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])

        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)
        scid = decoded_tx['vsc_ccout'][0]['scid']
        scid2 = decoded_tx['vsc_ccout'][1]['scid']

        self.nodes[0].generate(1)
        self.sync_all()

        print("Verify all nodes see the new SC...")
        scinfo0=self.nodes[0].getscinfo(scid)['items'][0]
        scinfo1=self.nodes[1].getscinfo(scid)['items'][0]
        scinfo2=self.nodes[2].getscinfo(scid)['items'][0]
        assert_equal(scinfo0,scinfo1)
        assert_equal(scinfo0,scinfo2)
        print(scinfo0)
        print(scinfo1)
        print(scinfo2)

        #Try decode the SC with decoderawtransaction function
        print("Decode the new SC with decoderawtransaction function...")

        decoded_tx=self.nodes[0].decoderawtransaction(self.nodes[0].gettransaction(finalRawtx)['hex'])
        print(decoded_tx)

        assert(len(decoded_tx['vsc_ccout'])==2)
        assert_equal(decoded_tx['vsc_ccout'][0]['scid'],scid)
        assert_equal(decoded_tx['vsc_ccout'][0]['withdrawal epoch length'],sc_epoch)
        assert_equal(decoded_tx['vsc_ccout'][0]['wCertVk'],vk)
        assert_equal(decoded_tx['vsc_ccout'][0]['constant'],constant)
        assert_equal(decoded_tx['vsc_ccout'][0]['value'],sc_cr_amount)
        assert_equal(decoded_tx['vsc_ccout'][0]['address'],sc_address)
        assert_equal(decoded_tx['vsc_ccout'][1]['scid'],scid2)
        assert_equal(decoded_tx['vsc_ccout'][1]['withdrawal epoch length'],sc_epoch2)
        assert_equal(decoded_tx['vsc_ccout'][1]['value'],sc_cr_amount2)

        #Try create a FT
        print("Try create new FT to SC just created using fund cmd")
        inputs = []
        # Note: sc_ft_amount must be a multiple of 4
        sc_ft_amount = Decimal('10.00000000')
        sc_ft = [{"address": sc_address, "amount":sc_ft_amount, "scid": scid}]
        rawtx=self.nodes[0].createrawtransaction(inputs,{},[],[],sc_ft)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])

        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        print("Verify all nodes see the new FT...")
        scinfo0=self.nodes[0].getscinfo(scid)['items'][0]
        scinfo1=self.nodes[1].getscinfo(scid)['items'][0]
        scinfo2=self.nodes[2].getscinfo(scid)['items'][0]
        assert_equal(scinfo0,scinfo1)
        assert_equal(scinfo0,scinfo2)
        print(scinfo0)
        print(scinfo1)
        print(scinfo2)

        decoded_tx=self.nodes[0].decoderawtransaction(self.nodes[0].gettransaction(finalRawtx)['hex'])
        assert(len(decoded_tx['vsc_ccout'])==0)
        assert(len(decoded_tx['vft_ccout'])==1)
        assert_equal(decoded_tx['vft_ccout'][0]['scid'],scid)
        assert_equal(decoded_tx['vft_ccout'][0]['value'],sc_ft_amount)
        assert_equal(decoded_tx['vft_ccout'][0]['address'],sc_address)


        # Try create a CSW input
        print("Try create new CSW from the SC just created")

        print("Making SC ceased")
        self.nodes[0].generate(int(sc_epoch * 1.25))
        self.sync_all()

        csw_mc_address = self.nodes[0].getnewaddress()

        sc_csw_amount = sc_ft_amount/5

        sc_csws = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "nullifier": generate_random_field_element_hex(),
            # Temp hardcoded proof with valid structure
            # TODO: generate a real valid CSW proof
            "scProof": "927e725a39f1c219a458f02d27fb327cc9595985ed947553d979261261b96360b23633b747df8141bcb12076b75f654c35ba0869df74a236763fe0c070e6da2959c1a8c77330783e76e4ad5801818c5edb06567196813355bea5e08beaa5010088965b13b48cbf962106500727ba05b31b4f429076230a90384d18b0e5f395a87ea466704a56375d3a68e65777568881b432208029c12cda5d089f596cf91da14392ed6c619c195a6bebe04c2caba17443906fbf386bc4555b0b721a1ead0000007acd59b470379a38d8de9e82b54fdd1e4e8bd8b2059b62552814989c25f7e07c6261ebc6de8b4b875893a874df953594beb119d53fd74e33e09cb66ed717c393c3fd22f1b465332a17c3d934172fdd33d1c641a9121c5e762b6e59305d1d0100ec5aed56c4290c6bb57e1d1b5b2b1f861f9926403446482f72cede346c0feae2817a2f18b7a37a9b55a3e9deb2a555ffb0d9331cb320ce18aa99a2c2c025c3d28afc77c631263b91160b1f556a6d1d158a8d3c56ab61dc9396e6536094720000741ae2c1569b098231dce089680fb1e561d974ce4f4e00cbe1150281ce12dd561be12a7fefcb30f62d3c8934926ae4eb4a4cb4378dd2568648ff12a7c36302be4d5a578dc360a3125b0c1427fb6b55a067f01d24d616c954bce363a8ef1001003a1ebe119da0561bf1d3294819759677fbd37dbac403662e263bfa71a4992228557a31d2d9ce0a7ffcbe91aa57f38cae7b51ef2681b16f275c0f87c89fbc2060690ac77dc1d3d20b7d3c6b5af1c92ee96e61b6635e343c3976112eb4ec91000000fe60207ddf86be08604c41f46f2e3740b479cad9fd1cb5f8c589595ba3d50f6c3984bbe707d460a0e27d4ec90d89a3476c647a6ea262b910dcb267325c375c713ff7031fe3a200130060bf09900e2e5244f88355a2a0587b068caae7f65b01005f0fb082380604a78c66e21681c2c7f3f59042c7b4495435b8d972bbb535ae8dd09ea8232b0161dc3a13f4a718b5a7fa4cb01d6625e38d73032baf3a9ffcff5a7493a27eeab25c97bee8eddf2fd2c9e9dd1bd1813c22b046c01caccc7478000000"
        }]

        sc_csw_tx_outs = {self.nodes[0].getnewaddress(): sc_csw_amount - Decimal('0.00001000')}

        # Create Tx with a single CSW input
        print("Create Tx with a single CSW input")
        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws, [], [])
        sigRawtx = self.nodes[0].signrawtransaction(rawtx)
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])

        self.sync_all()

        print("Check the getrawtransaction result for Tx with CSW input")
        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)
        assert_equal(len(decoded_tx['vcsw_ccin']), 1)
        assert_equal(decoded_tx['vcsw_ccin'][0]['value'], sc_csws[0]['amount'])
        assert_equal(decoded_tx['vcsw_ccin'][0]['scId'], sc_csws[0]['scId'])
        assert_equal(decoded_tx['vcsw_ccin'][0]['nullifier'], sc_csws[0]['nullifier'])
        assert_equal(decoded_tx['vcsw_ccin'][0]['scriptPubKey']['addresses'][0], sc_csws[0]['senderAddress'])
        assert_equal(decoded_tx['vcsw_ccin'][0]['scProof'], sc_csws[0]['scProof'])


        # Create a Tx with cc outputs and CSW input, and fund it via cmd
        csw_mc_address = self.nodes[0].getnewaddress()

        sc_csws2 = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "nullifier": generate_random_field_element_hex(),
            # Temp hardcoded proof with valid structure
            # TODO: generate a real vali CSW proof
            "scProof": "927e725a39f1c219a458f02d27fb327cc9595985ed947553d979261261b96360b23633b747df8141bcb12076b75f654c35ba0869df74a236763fe0c070e6da2959c1a8c77330783e76e4ad5801818c5edb06567196813355bea5e08beaa5010088965b13b48cbf962106500727ba05b31b4f429076230a90384d18b0e5f395a87ea466704a56375d3a68e65777568881b432208029c12cda5d089f596cf91da14392ed6c619c195a6bebe04c2caba17443906fbf386bc4555b0b721a1ead0000007acd59b470379a38d8de9e82b54fdd1e4e8bd8b2059b62552814989c25f7e07c6261ebc6de8b4b875893a874df953594beb119d53fd74e33e09cb66ed717c393c3fd22f1b465332a17c3d934172fdd33d1c641a9121c5e762b6e59305d1d0100ec5aed56c4290c6bb57e1d1b5b2b1f861f9926403446482f72cede346c0feae2817a2f18b7a37a9b55a3e9deb2a555ffb0d9331cb320ce18aa99a2c2c025c3d28afc77c631263b91160b1f556a6d1d158a8d3c56ab61dc9396e6536094720000741ae2c1569b098231dce089680fb1e561d974ce4f4e00cbe1150281ce12dd561be12a7fefcb30f62d3c8934926ae4eb4a4cb4378dd2568648ff12a7c36302be4d5a578dc360a3125b0c1427fb6b55a067f01d24d616c954bce363a8ef1001003a1ebe119da0561bf1d3294819759677fbd37dbac403662e263bfa71a4992228557a31d2d9ce0a7ffcbe91aa57f38cae7b51ef2681b16f275c0f87c89fbc2060690ac77dc1d3d20b7d3c6b5af1c92ee96e61b6635e343c3976112eb4ec91000000fe60207ddf86be08604c41f46f2e3740b479cad9fd1cb5f8c589595ba3d50f6c3984bbe707d460a0e27d4ec90d89a3476c647a6ea262b910dcb267325c375c713ff7031fe3a200130060bf09900e2e5244f88355a2a0587b068caae7f65b01005f0fb082380604a78c66e21681c2c7f3f59042c7b4495435b8d972bbb535ae8dd09ea8232b0161dc3a13f4a718b5a7fa4cb01d6625e38d73032baf3a9ffcff5a7493a27eeab25c97bee8eddf2fd2c9e9dd1bd1813c22b046c01caccc7478000000"
        }]

        # a fw transfer to scid2 (non ceased)
        sc_ft_amount = Decimal('16.0')
        sc_ft2 = [{"address": "ffff", "amount":sc_ft_amount, "scid": scid2}]

        # another sc creation, just to have a different cc output
        vk2 = mcTest.generate_params("sc2")
        cswVk2 = mcTest.generate_params("csw2")
        constant2 = generate_random_field_element_hex()
        sc_cr2 = [{
            "epoch_length": sc_epoch,
            "amount": Decimal("4.0"),
            "address": "ccc",
            "wCertVk": vk2,
            "wCeasedVk": cswVk2,
            "constant": constant2
        }]

        # outputs to different nodes
        taddr0 = self.nodes[0].getnewaddress()
        taddr1 = self.nodes[1].getnewaddress()
        outputs = { taddr0 : 1.0, taddr1 : 2.0 }

        print("Create Tx with cc outputs and CSW input, and fund it via cmd")
        try:
            rawtx = self.nodes[0].createrawtransaction([], outputs, sc_csws2, sc_cr2, sc_ft2)
            funded_tx = self.nodes[0].fundrawtransaction(rawtx)
            sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
            finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
            decoded_tx = self.nodes[0].decoderawtransaction(sigRawtx['hex'])
        except JSONRPCException,e:
            errorString = e.error['message']
            print "===> ", errorString
            assert(False)

        self.sync_all()

        assert_equal(len(decoded_tx['vcsw_ccin']), 1)
        assert_equal(len(decoded_tx['vsc_ccout']), 1)
        assert_equal(len(decoded_tx['vft_ccout']), 1)
        assert_equal(decoded_tx['vcsw_ccin'][0]['value'], sc_csws2[0]['amount'])
        assert_equal(decoded_tx['vsc_ccout'][0]['value'], sc_cr2[0]['amount'])
        assert_equal(decoded_tx['vft_ccout'][0]['value'], sc_ft2[0]['amount'])

        # check output of rpc cmd
        nlist_0 = self.nodes[0].listtxesbyaddress(taddr0)
        in_tot  = 0
        out_tot = 0

        for i in nlist_0[0]['vin']:
            in_tot = in_tot + i['value'] 
        for i in nlist_0[0]['vcsw_ccin']:
            in_tot = in_tot + i['value'] 

        for i in nlist_0[0]['vout']:
            out_tot = out_tot + i['value'] 
        for i in nlist_0[0]['vft_ccout']:
            out_tot = out_tot + i['value'] 
        for i in nlist_0[0]['vsc_ccout']:
            out_tot = out_tot + i['value'] 

        fee = in_tot - out_tot
        assert_equal(fee, nlist_0[0]['fees'])
        
        # Create Tx with single CSW input and single output. CSW input signed as "SINGLE"
        print("Create Tx with a single CSW input and single output. CSW input signed as 'SINGLE'.")
        sc_csws[0]['nullifier'] = generate_random_field_element_hex()
        
        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws, [], [])
        sigRawtx = self.nodes[0].signrawtransaction(rawtx, None, None, "SINGLE")
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])

        self.sync_all()

        print("Check the getrawtransaction result for Tx with CSW input signed as 'SINGLE'.")
        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)
        assert_equal(len(decoded_tx['vcsw_ccin']), 1)
        assert_equal(decoded_tx['vcsw_ccin'][0]['value'], sc_csws[0]['amount'])
        assert_equal(decoded_tx['vcsw_ccin'][0]['scId'], sc_csws[0]['scId'])
        assert_equal(decoded_tx['vcsw_ccin'][0]['nullifier'], sc_csws[0]['nullifier'])
        assert_equal(decoded_tx['vcsw_ccin'][0]['scriptPubKey']['addresses'][0], sc_csws[0]['senderAddress'])
        assert_equal(decoded_tx['vcsw_ccin'][0]['scProof'], sc_csws[0]['scProof'])


        # Try to create Tx with 2 CSW inputs and single output. CSW input signed as "SINGLE".
        # Should fail, because the second CSW input has no corresponding output.
        multiple_sc_csws = [sc_csws[0].copy(), sc_csws[0].copy()]
        multiple_sc_csws[0]['nullifier'] = generate_random_field_element_hex()
        multiple_sc_csws[1]['nullifier'] = generate_random_field_element_hex()
        multiple_sc_csws[0]['amount'] = sc_csw_amount / 2
        multiple_sc_csws[1]['amount'] = sc_csw_amount / 2

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, multiple_sc_csws, [], [])
        sigRawtx = self.nodes[0].signrawtransaction(rawtx, None, None, "SINGLE")

        print("Check that Tx with 2 CSW inputs and 1 output signing as 'SINGLE' failed.")
        assert_equal(sigRawtx['complete'], False)
        # has no signature for the second CSW input, because we DON'T have corresponding output
        assert_equal(len(sigRawtx['errors']), 1)
        assert_equal(sigRawtx['errors'][0]['cswIndex'], 1)


        # Create Tx with single CSW input and single output. CSW input signed as "NONE"
        print("Create Tx with a single CSW input and single output. CSW input signed as 'NONE'.")
        sc_csws[0]['nullifier'] = generate_random_field_element_hex()

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws, [], [])
        sigRawtx = self.nodes[0].signrawtransaction(rawtx, None, None, "NONE")
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])

        self.sync_all()

        print("Check the getrawtransaction result for Tx with CSW input signed as 'NONE'.")
        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)
        assert_equal(len(decoded_tx['vcsw_ccin']), 1)
        assert_equal(decoded_tx['vcsw_ccin'][0]['value'], sc_csws[0]['amount'])
        assert_equal(decoded_tx['vcsw_ccin'][0]['scId'], sc_csws[0]['scId'])
        assert_equal(decoded_tx['vcsw_ccin'][0]['nullifier'], sc_csws[0]['nullifier'])
        assert_equal(decoded_tx['vcsw_ccin'][0]['scriptPubKey']['addresses'][0], sc_csws[0]['senderAddress'])
        assert_equal(decoded_tx['vcsw_ccin'][0]['scProof'], sc_csws[0]['scProof'])


        # Try to create CSW with the nullifier which already exists in the mempool Tx
        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws, [], [])
        sigRawtx = self.nodes[0].signrawtransaction(rawtx)

        error_occurred = False
        try:
            self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException, e:
            error_occurred = True

        assert_true(error_occurred)


        # Try to create CSW that spends more coins that available for the given SC balance
        print("Check that Tx with CSW input amount greater then Sc mature balance is invalid.")
        sc_csws[0]['nullifier'] = generate_random_field_element_hex()
        sc_csws[0]['amount'] = self.nodes[0].getscinfo(scid)['items'][0]['balance'] + Decimal('0.00000001')
        sc_csw_tx_outs = {self.nodes[0].getnewaddress(): sc_csws[0]['amount'] - Decimal('0.00001000')}

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws, [], [])
        sigRawtx = self.nodes[0].signrawtransaction(rawtx)

        error_occurred = False
        try:
            self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException, e:
            error_occurred = True

        assert_true(error_occurred, "CSW with more coins that available for the SC balance "
                                    "expected to be rejected by the mempool.")


        # Try to create CSW that spends more coins that available for the given SC balance (considering mempool)
        print("Check that Tx with CSW input amount greater then [Sc mature balance minus mempool Txs values] is invalid.")
        sc_csws[0]['nullifier'] = generate_random_field_element_hex()
        # SC balance equal to sc_cr_amount + sc_csw_amount * 5
        # Mempool contains 3 Txs with `sc_csw_amount` coins each.
        sc_csws[0]['amount'] = self.nodes[0].getscinfo(scid)['items'][0]['balance'] - sc_csw_amount + Decimal('0.00000001')
        sc_csw_tx_outs = {self.nodes[0].getnewaddress(): sc_csws[0]['amount'] - Decimal('0.00001000')}

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws, [], [])
        sigRawtx = self.nodes[0].signrawtransaction(rawtx)

        error_occurred = False
        try:
            self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException, e:
            error_occurred = True

        assert_true(error_occurred, "CSW with more coins that available for the SC balance considering mempool "
                                    "expected to be rejected by the mempool.")



if __name__ == '__main__':
    RawTransactionsTest().main()
