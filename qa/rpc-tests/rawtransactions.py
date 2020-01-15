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

        print("Node 1 sends 10 coins to node 0 to have a UTXO...")
        txid=self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(),10.0)
        self.sync_all()

        self.nodes[0].generate(1)
        self.sync_all()

        sc_address="0000000000000000000000000000000000000000000000000000000000000abc"
        scid="0000000000000000000000000000000000000000000000000000000000000022"
        sc_epoch=123
        sc_amount=Decimal('10.00000000')
        sc=[{"scid": scid,"epoch_length": sc_epoch}]

        #Try create a SC with no inputs
        print("Try create a SC with no inputs...")

        rawtx=self.nodes[0].createrawtransaction([],{},sc)
        sigRawtx = self.nodes[0].signrawtransaction(rawtx)
        try:
            finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException,e:
            errorString = e.error['message']
        assert_equal("vin-empty" in errorString, True)

        #Try create a SC with no FT
        print("Try create a SC with no FT...")

        inputs = [{'txid': txid, 'vout': 1}]
        rawtx=self.nodes[0].createrawtransaction(inputs,{},sc)
        sigRawtx = self.nodes[0].signrawtransaction(rawtx)
        try:
            finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException,e:
            errorString = e.error['message']
        assert_equal("missing-fwd-transfer" in errorString, True)

        #Create a SC
        print("Create a SC of id: "+scid)

        decodedTx=self.nodes[0].decoderawtransaction(self.nodes[0].gettransaction(txid)['hex'])
        vout = {}
        for outpoint in decodedTx['vout']:
            if outpoint['value'] == Decimal('10.0'):
                vout = outpoint
                break;

        inputs = [{'txid': txid, 'vout': vout['n']}]
        sc_ft=[{"address": sc_address, "amount":sc_amount, "scid": scid}]
        rawtx=self.nodes[0].createrawtransaction(inputs,{},sc,sc_ft)
        sigRawtx = self.nodes[0].signrawtransaction(rawtx)
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])

        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        print("Verify all nodes see the new SC...")
        scinfo0=self.nodes[0].getscinfo(scid)
        scinfo1=self.nodes[1].getscinfo(scid)
        scinfo2=self.nodes[2].getscinfo(scid)
        assert_equal(scinfo0,scinfo1)
        assert_equal(scinfo0,scinfo2)
        print(scinfo0)
        print(scinfo1)
        print(scinfo2)

        #Try decode the SC with decoderawtransaction function
        print("Decode the new SC with decoderawtransaction function...")

        decodedTx=self.nodes[0].decoderawtransaction(self.nodes[0].gettransaction(finalRawtx)['hex'])
        print(decodedTx)

        assert(len(decodedTx['vsc_ccout'])==1)
        assert_equal(decodedTx['vsc_ccout'][0]['scid'],scid)
        assert_equal(decodedTx['vsc_ccout'][0]['withdrawal epoch length'],sc_epoch)

        assert(len(decodedTx['vft_ccout'])==1)
        assert_equal(decodedTx['vft_ccout'][0]['scid'],scid)
        assert_equal(decodedTx['vft_ccout'][0]['value'],sc_amount)
        assert_equal(decodedTx['vft_ccout'][0]['address'],sc_address)

        #Try create same SC
        print("Try create a SC with the same scid...")

        txid2=self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(),10.0)
        self.sync_all()

        self.nodes[0].generate(1)
        self.sync_all()

        decodedTx=self.nodes[0].decoderawtransaction(self.nodes[0].gettransaction(txid2)['hex'])
        vout = {}
        for outpoint in decodedTx['vout']:
            if outpoint['value'] == Decimal('10.0'):
                vout = outpoint
                break;

        inputs = [{'txid': txid2, 'vout': vout['n']}]
        sc_ft=[{"address": sc_address, "amount": sc_amount, "scid": scid}]
        rawtx=self.nodes[0].createrawtransaction(inputs,{},sc,sc_ft)
        sigRawtx = self.nodes[0].signrawtransaction(rawtx)

        try:
            finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        except:
            print("Duplicate scid")
            error=True
        assert_true(error)

        #Try create a FT
        print("Try create new FT of 10 coins")

        rawtx=self.nodes[0].createrawtransaction(inputs,{},[],sc_ft)
        sigRawtx = self.nodes[0].signrawtransaction(rawtx)
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])

        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        print("Verify all nodes see the new FT...")
        scinfo0=self.nodes[0].getscinfo(scid)
        scinfo1=self.nodes[1].getscinfo(scid)
        scinfo2=self.nodes[2].getscinfo(scid)
        assert_equal(scinfo0,scinfo1)
        assert_equal(scinfo0,scinfo2)
        print(scinfo0)
        print(scinfo1)
        print(scinfo2)

        decodedTx=self.nodes[0].decoderawtransaction(self.nodes[0].gettransaction(finalRawtx)['hex'])
        assert(len(decodedTx['vsc_ccout'])==0)
        assert(len(decodedTx['vft_ccout'])==1)
        assert_equal(decodedTx['vft_ccout'][0]['scid'],scid)
        assert_equal(decodedTx['vft_ccout'][0]['value'],sc_amount)
        assert_equal(decodedTx['vft_ccout'][0]['address'],sc_address)


if __name__ == '__main__':
    RawTransactionsTest().main()
