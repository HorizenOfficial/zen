#!/usr/bin/env python2

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_greater_than, \
    initialize_chain_clean, start_nodes, start_node, connect_nodes_bi, \
    stop_nodes, sync_blocks, sync_mempools, wait_bitcoinds
from binascii import hexlify, unhexlify
import logging
from decimal import Decimal


class NullDataTest (BitcoinTestFramework):
    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_network(self, split=False):
        self.nodes = start_nodes(3, self.options.tmpdir)
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 0, 2)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        #logging.basicConfig()
        #logging.getLogger("BitcoinRPC").setLevel(logging.DEBUG)
        self.nodes[0].generate(1)
        self.sync_all()
        self.nodes[1].generate(106)
        self.sync_all()
        sender_address = self.nodes[2].getnewaddress()
        self.nodes[0].sendtoaddress(sender_address,1.5)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()        


        # Use createrawtransaction to create a template
        # We will later replace one of the outputs with a
        # NULLDATA transaction 
        first_unspent = self.nodes[2].listunspent()[0]
        txid = first_unspent['txid']
        vout = first_unspent['vout']
        input_amount = first_unspent['amount']
        change_amount = input_amount - Decimal("0.0001")
        change_address = sender_address
        dummy_address = self.nodes[0].getnewaddress()
        tx =  self.nodes[2]\
        .createrawtransaction([{"txid": txid, "vout": vout}], {dummy_address: 0 , \
                               change_address:change_amount})

        decoded_tx = self.nodes[2].decoderawtransaction(tx)
        
        # Forging a dummy script and replacing it with an OP_RETURN script.
        dummy_script = ""
        data = "Zencash is awesome!"
        for vo in decoded_tx['vout']:
            if vo['valueZat']==0:
                dummy_script = vo['scriptPubKey']['hex']
                break
        dummy_script = hexlify(chr(len(dummy_script)/2))+dummy_script

        # Manually forging a valid OP_RETURN script with OP_CHECKBLOCKHEIGHT at the end.
        newScriptPubKey = "6a" + hexlify(chr(len(data))) + hexlify(data)\
        +"20bb1acf2c1fc1228967a611c7db30632098f0c641855180b5fe23793b72eea50d"\
        +"00b4" # Genesis Block hash + ParamHeight=0 + OP_CHECKBLOCKHEIGHT
        newScriptPubKey = hexlify(chr(len(unhexlify(newScriptPubKey)))) + newScriptPubKey

        # Replace the dummy script with the OP_RETURN script in our transaction.
        tx = tx.replace(dummy_script,newScriptPubKey)
        
        # Signing and broadcasting the transaction
        tx = self.nodes[2].signrawtransaction(tx)['hex']
        returned_txid = self.nodes[2].sendrawtransaction(tx)

        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all() 
        
        returnedTx = self.nodes[2].decoderawtransaction( self.nodes[2].gettransaction(returned_txid)['hex'])
        returned_data = ""

        assert len(returnedTx['vout'])!=0
        
        for vo in returnedTx['vout']:
            if hexlify(data) in vo['scriptPubKey']['asm']:
                returned_data = unhexlify( vo['scriptPubKey']['asm'].split()[1] )
                break
        
        assert returned_data != ""
        # Make sure the data is correctly stored on the blockchain.
        assert returned_data == data
        
if __name__ == '__main__':
    NullDataTest().main()
