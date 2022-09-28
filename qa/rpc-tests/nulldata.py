#!/usr/bin/env python3

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_false, hex_str_to_bytes, hex_str_to_str, \
    initialize_chain_clean, start_nodes, connect_nodes_bi, str_to_hex_str
from decimal import Decimal


class NullDataTest (BitcoinTestFramework):
    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_network(self, split=False):
        self.nodes = start_nodes(3, self.options.tmpdir,
            extra_args = [
                ["-logtimemicros", "-debug=py", "-debug=cbh"],
                ["-logtimemicros", "-debug=py", "-debug=cbh"],
                ["-logtimemicros", "-debug=py", "-debug=cbh"]
            ])
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 0, 2)
        self.is_network_split = False
        self.sync_all()

    def mark_logs(self, msg):
        print(msg)
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)
        self.nodes[2].dbg_log(msg)

    def run_test(self):
        #logging.basicConfig()
        #logging.getLogger("BitcoinRPC").setLevel(logging.DEBUG)

        def create_null_tx_with_custom_data(node_from, node_to, data): 

            # Use createrawtransaction to create a template
            # We will later replace one of the outputs with a
            # NULLDATA transaction 
            utxos = node_from.listunspent()
            if len(utxos) == 0:
                print("No utxo for node")
                assert_false(True)

            first_unspent = utxos[0]
            txid = first_unspent['txid']
            vout = first_unspent['vout']
            input_amount = first_unspent['amount']
            change_amount = input_amount - Decimal("0.0001")
            change_address = node_from.getnewaddress()
            dummy_address  = node_to.getnewaddress()
            tx =  node_from.createrawtransaction([{"txid": txid, "vout": vout}], {dummy_address: 0 , \
                                   change_address:change_amount})

            decoded_tx = node_from.decoderawtransaction(tx)
            
            # Forging a dummy script and replacing it with an OP_RETURN script.
            dummy_script = ""
            for vo in decoded_tx['vout']:
                if vo['valueZat']==0:
                    dummy_script = vo['scriptPubKey']['hex']
                    break

            # prepend script size
            dummy_script = format(len(dummy_script)//2, 'x') + dummy_script

            # Manually forging a valid OP_RETURN script with OP_CHECKBLOCKHEIGHT at the end.
            newScriptPubKey = "6a"

            # maximum size for data to be stored after OP_RETURN is 75 (0x4b)
            # beyond that size we have to push 0x4c, that is a specific OP_PUSHDATA1 opcode to be
            # prepended for larger values
            if len(data) > 75:
                newScriptPubKey += "4c"

            newScriptPubKey = newScriptPubKey + str_to_hex_str(chr(len(data)))\
            + str_to_hex_str(data)\
            +"20bb1acf2c1fc1228967a611c7db30632098f0c641855180b5fe23793b72eea50d"\
            +"00b4" # Genesis Block hash + ParamHeight=0 + OP_CHECKBLOCKHEIGHT

            # prepend script size
            newScriptPubKey = str_to_hex_str(chr(len(hex_str_to_bytes(newScriptPubKey)))) + newScriptPubKey

            # Replace the dummy script with the OP_RETURN script in our transaction.
            tx = tx.replace(dummy_script, newScriptPubKey)
            return tx


        def send_null_tx_and_check(node, tx, data):        
            # Signing and broadcasting the transactions
            tx = node.signrawtransaction(tx)['hex']
            returned_txid = node.sendrawtransaction(tx)


            self.sync_all()
 
            node.generate(1)
            self.sync_all() 

            returnedTx = node.decoderawtransaction(node.gettransaction(returned_txid)['hex'])
            returned_data = ""

            assert len(returnedTx['vout'])!=0
            
            for vo in returnedTx['vout']:
                if str_to_hex_str(data) in vo['scriptPubKey']['asm']:
                    returned_data = hex_str_to_str(vo['scriptPubKey']['asm'].split()[1])
                    break

            assert returned_data != ""
            # Make sure the data is correctly stored on the blockchain.
            assert returned_data == data


        def doTest():
            tx1 = create_null_tx_with_custom_data(self.nodes[2], self.nodes[0], data1)
            send_null_tx_and_check(self.nodes[2], tx1, data1)
            self.sync_all()

            tx2 = create_null_tx_with_custom_data(self.nodes[2], self.nodes[0], data2)
            send_null_tx_and_check(self.nodes[2], tx2, data2)
            self.sync_all()

            tx3 = create_null_tx_with_custom_data(self.nodes[2], self.nodes[0], data3)
            try:
                send_null_tx_and_check(self.nodes[2], tx3, data3)
            except JSONRPCException as e:
                # this will be refused because data size exceeds max
                errorString = e.error['message']
                assert_equal("scriptpubkey" in errorString, True);
                print("...Ok, refused data exceeding max size")


        # ----------- Tests start ----------------------
        self.nodes[0].generate(1)
        self.sync_all()
        self.nodes[1].generate(106)
        self.sync_all()
        sender_address = self.nodes[2].getnewaddress()

        # create sone coins for Node2 in order for him to have some UTXO
        self.nodes[0].sendtoaddress(sender_address, 1.0)
        self.sync_all()

        self.nodes[0].generate(1)
        self.sync_all()        

        #       0       8      16      24      32      40      48      56      64      72      80
        #       |       |       |       |       |       |       |       |       |       |       |
        #       ---------------------------------------------------------------------------------
        data1 = "Horizen is awesome!"
        data2 = "Msg for reaching a size of 76 bytes, one more than the limit for small data!"
        data3 = "Let's now create a large message string for reaching a size bigger than 80 bytes limit!"

        # perform a first pass before rp fork fix 
        print("first pass before rp fork fix: chain height = ", self.nodes[0].getblockcount())
        doTest()

        self.nodes[1].generate(300)
        self.sync_all()

        # perform a second pass after rp fork fix 
        print("second pass after rp fork fix: chain height = ", self.nodes[0].getblockcount())
        doTest()

if __name__ == '__main__':
    NullDataTest().main()
