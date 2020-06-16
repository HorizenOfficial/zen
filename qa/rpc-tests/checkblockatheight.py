#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.script import OP_DUP, OP_EQUALVERIFY, OP_HASH160, OP_EQUAL, hash160, OP_CHECKSIG, OP_CHECKBLOCKATHEIGHT
from test_framework.util import assert_equal, assert_greater_than, initialize_chain_clean, \
    start_nodes, start_node, connect_nodes, stop_node, stop_nodes, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, p2p_port, check_json_precision, disconnect_nodes
from test_framework.script import CScript
from test_framework.mininode import CTransaction, ToHex
from test_framework.util import hex_str_to_bytes, bytes_to_hex_str
import traceback
from binascii import unhexlify
import cStringIO
import os,sys
import shutil
from decimal import Decimal
import binascii
import codecs
'''
from random import randint
import logging
import pprint
import struct
import array
'''

import time

NUMB_OF_NODES = 4

# the scripts will not be checked if we have more than this depth of referenced block
FINALITY_SAFE_DEPTH = 150

# 0 means do not check any minimum age for referenced blocks in scripts
FINALITY_MIN_AGE = 75 

CBH_DELTA = 300

class headers(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False, minAge=FINALITY_MIN_AGE):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
            extra_args = [
                ["-debug=cbh", "-cbhsafedepth="+str(FINALITY_SAFE_DEPTH), "-cbhminage="+str(minAge)],
                ["-debug=cbh", "-cbhsafedepth="+str(FINALITY_SAFE_DEPTH), "-cbhminage="+str(minAge)],
                ["-debug=cbh", "-cbhsafedepth="+str(FINALITY_SAFE_DEPTH), "-cbhminage="+str(minAge)],
                ["-debug=cbh", "-cbhsafedepth="+str(FINALITY_SAFE_DEPTH), "-cbhminage="+str(minAge)]
            ])

        if not split:
            # 2 and 3 are joint only if split==false
            connect_nodes_bi(self.nodes, 2, 3)
            connect_nodes_bi(self.nodes, 3, 2)
            sync_blocks(self.nodes[2:NUMB_OF_NODES])
            sync_mempools(self.nodes[2:NUMB_OF_NODES])

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 0)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 1)
        self.is_network_split = split
        self.sync_all()

    def split_network(self):
        # Split the network of 4 nodes into nodes 0-1-2 and 3.
        assert not self.is_network_split
        disconnect_nodes(self.nodes[2], 3)
        disconnect_nodes(self.nodes[3], 2)
        self.is_network_split = True


    def join_network(self):
        #Join the (previously split) network pieces together: 0-1-2-3
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 2)
#        self.sync_all()
        sync_blocks(self.nodes, 1, False, 5)
        self.is_network_split = False

    def mark_logs(self, msg):
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)
        self.nodes[2].dbg_log(msg)
        self.nodes[3].dbg_log(msg)

    def swap_bytes(self, input_buf):
        return codecs.encode(codecs.decode(input_buf, 'hex')[::-1], 'hex').decode()

    def is_in_block(self, tx, bhash, node_idx = 0):
        blk_txs = self.nodes[node_idx].getblock(bhash, True)['tx']
        for i in blk_txs:
            if (i == tx): 
                return True
        return False

    def is_in_mempool(self, tx, node_idx = 0): 
        mempool = self.nodes[node_idx].getrawmempool()
        for i in mempool:
            if (i == tx): 
                return True
        return False

    def run_test(self):
        blocks = []
        self.bl_count = 0

        blocks.append(self.nodes[1].getblockhash(0))

        small_target_h = 3

        s = "  Node1 generates %d blocks" % (CBH_DELTA + small_target_h)
        print(s)
        print
        self.mark_logs(s)
        blocks.extend(self.nodes[1].generate(CBH_DELTA + small_target_h))
        self.sync_all()

        #-------------------------------------------------------------------------------------------------------
        print "Trying to send a tx with a scriptPubKey referencing a block too recent..."
        #-------------------------------------------------------------------------------------------------------
        # Create a tx having in its scriptPubKey a custom referenced block in the CHECKBLOCKATHEIGHT part 

        # select necessary utxos for doing the PAYMENT
        usp = self.nodes[1].listunspent()

        PAYMENT = Decimal('1.0')
        FEE     = Decimal('0.00005')
        amount = Decimal('0')
        inputs  = []

        print "  Node1 sends %f coins to Node2" % PAYMENT

        for x in usp:
            amount += Decimal(x['amount']) 
            inputs.append( {"txid":x['txid'], "vout":x['vout']})
            if amount >= PAYMENT+FEE:
                break

        outputs = {self.nodes[1].getnewaddress(): (Decimal(amount) - PAYMENT - FEE), self.nodes[2].getnewaddress(): PAYMENT}
        rawTx   = self.nodes[1].createrawtransaction(inputs, outputs)

        # build an object from the raw tx in order to be able to modify it
        tx_01 = CTransaction()
        f = cStringIO.StringIO(unhexlify(rawTx))
        tx_01.deserialize(f)

        decodedScriptOrig = self.nodes[1].decodescript(binascii.hexlify(tx_01.vout[1].scriptPubKey))

        scriptOrigAsm = decodedScriptOrig['asm']

#        print "Original scriptPubKey asm 1:   ", scriptOrigAsm
#        print

        # store the hashed script, it is reused
        params = scriptOrigAsm.split()
        hash_script = hex_str_to_bytes(params[2])

        # new referenced block height
        modTargetHeigth = CBH_DELTA + small_target_h - FINALITY_MIN_AGE + 5

        # new referenced block hash
        modTargetHash = hex_str_to_bytes(self.swap_bytes(blocks[modTargetHeigth]))
        
        # build modified script
        modScriptPubKey = CScript([OP_DUP, OP_HASH160, hash_script, OP_EQUALVERIFY, OP_CHECKSIG, modTargetHash, modTargetHeigth, OP_CHECKBLOCKATHEIGHT])

        tx_01.vout[1].scriptPubKey = modScriptPubKey
        tx_01.rehash()

        decodedScriptMod = self.nodes[1].decodescript(binascii.hexlify(tx_01.vout[1].scriptPubKey))
        print "  Modified scriptPubKey in tx 1: ", decodedScriptMod['asm']

        signedRawTx = self.nodes[1].signrawtransaction(ToHex(tx_01))

        h = self.nodes[1].getblockcount()
        assert_greater_than(FINALITY_MIN_AGE, h - modTargetHeigth)

        #raw_input("\npress enter to go on ..")
        try:
            txid = self.nodes[1].sendrawtransaction(signedRawTx['hex'])
            print "  Tx sent: ", txid
            # should fail, therefore force test failure
            assert_equal(True, False)

        except JSONRPCException,e:
            print "  ==> tx has been rejected as expected:"
            print "      referenced block height=%d, chainActive.height=%d, minimumAge=%d" % (modTargetHeigth, h, FINALITY_MIN_AGE)
            print

        #-------------------------------------------------------------------------------------------------------
        print "Check that small digit height for referenced blocks works in scriptPubKey"
        #-------------------------------------------------------------------------------------------------------
        # check that small height works for 'check block at height' in script
        #--------------------------------------------------------------------
        node1_pay = Decimal('0.5')
        s = "  Node1 sends %f coins to Node3 for checking script in tx" % node1_pay
        print s
        self.mark_logs(s)

        tx1 = self.nodes[1].sendtoaddress(self.nodes[3].getnewaddress(), node1_pay)
        print "  ==> tx sent: ", tx1

        sync_mempools(self.nodes[0:4])

        assert_equal(self.is_in_mempool(tx1), True)

        script = self.nodes[1].getrawtransaction(tx1, 1)['vout'][0]['scriptPubKey']['asm']
        tokens = script.split()
        small_h = int(tokens[6])
        small_h_hash = self.swap_bytes(tokens[5])

        print "  ScriptPubKey: ", script

        assert_equal(small_h, small_target_h)
        assert_equal(small_h_hash, blocks[int(small_h)])

        print("  Node1 generating 1 honest block")
        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        print "  | Node0 balance: ", self.nodes[0].getbalance()
        print "  | Node1 balance: ", self.nodes[1].getbalance()
        print "  | Node2 balance: ", self.nodes[2].getbalance()
        print "  | Node3 balance: ", self.nodes[3].getbalance()

        # check tx is no more in mempool
        assert_equal(self.is_in_mempool(tx1), False)

        # check tx is in the block just mined
        assert_equal(self.is_in_block(tx1, blocks[-1], 3), True)

        # check the balance is the one expected
        assert_equal(self.nodes[3].getbalance(), node1_pay)

        s = "  Node3 sends 0.25 coins to Node2 for checking script in tx"
        print s
        self.mark_logs(s)

        tx2 = self.nodes[3].sendtoaddress(self.nodes[2].getnewaddress(), 0.25)
        print "  ==> tx sent: ", tx2

        sync_mempools(self.nodes[0:4])

        assert_equal(self.is_in_mempool(tx2), True)

        print("  Node1 generating 1 honest block")
        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        print "  | Node0 balance: ", self.nodes[0].getbalance()
        print "  | Node1 balance: ", self.nodes[1].getbalance()
        print "  | Node2 balance: ", self.nodes[2].getbalance()
        print "  | Node3 balance: ", self.nodes[3].getbalance()

        # check tx is no more in mempool
        assert_equal(self.is_in_mempool(tx2), False)

        # check tx is in the block just mined
        assert_equal(self.is_in_block(tx2, blocks[-1], 1), True)

        print "  ==> OK, small digit height works in scripts:"
        print
        #--------------------------------------------------------------------
        
        print "Verify that a legal tx, when the referenced block is reverted, becomes invalid until the necessary depth of the referenced block height has been reached..."
        print "  Restarting network with -cbhminage=0"
        # restart Nodes and check their balance: node1 does not have 1000 coins but node2 does not have either
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False, 0)

        chunks = 305
        s = "  Node0 generates %d blocks" % chunks
        print(s)
        self.mark_logs(s)
        blocks.extend(self.nodes[0].generate(chunks)) 
        self.sync_all()

        print "  Split network: (0)---(1)---(2)   (3)"
        self.split_network()

        print "  Node0 generating 1 honest block"
        blocks.extend(self.nodes[0].generate(1)) 
        sync_blocks(self.nodes, 1, False, 5)
#        self.sync_all()
#        time.sleep(5)

        # we will perform on attack aimed at reverting from this block (latest generated) upward
        hash_attacked = blocks[-1];
        h_attacked = self.nodes[1].getblockcount()
        assert hash_attacked == blocks[h_attacked]
        hash_attacked_swapped = self.swap_bytes(hash_attacked)
        hex_s = "%04x" % h_attacked
        h_attacked_swapped = self.swap_bytes(hex_s)
        h_safe_estimated = h_attacked+FINALITY_SAFE_DEPTH+1

        print "  Honest network has current h[%d]" % h_attacked

        # we will create a transaction whose output will have a CHECKBLOCKATHEIGHT on this block
        h_checked = h_attacked - CBH_DELTA
        hex_s = "%04x" % h_checked
        h_checked_swapped = self.swap_bytes(hex_s)
        hash_checked = blocks[h_checked]
        hash_checked_swapped = self.swap_bytes(hash_checked)

        # select necessary utxos for doing the PAYMENT, there might be a lot of them
        usp = self.nodes[1].listunspent()

        PAYMENT = Decimal('1000.0')
        FEE     = Decimal('0.00005')
        amount = Decimal('0')
        inputs  = []

        for x in usp:
            amount += Decimal(x['amount']) 
            inputs.append( {"txid":x['txid'], "vout":x['vout']})
            if amount >= PAYMENT+FEE:
                break

        print"  Creating raw tx referencing the current block %d where Node1 sends %f coins to Node2..." % (h_attacked, PAYMENT)

        outputs = {self.nodes[1].getnewaddress(): (Decimal(amount) - PAYMENT - FEE), self.nodes[2].getnewaddress(): PAYMENT}
        rawTx   = self.nodes[1].createrawtransaction(inputs, outputs)

        # replace hash and h referenced in tx's script with the ones of target block 
        from_buf = str(hash_checked_swapped)+'02'+str(h_checked_swapped)
        to_buf   = str(hash_attacked_swapped)+'02'+str(h_attacked_swapped)
        rawTxReplaced = rawTx.replace(from_buf, to_buf)

        # modifying just one vout is enough for tampering the whole tx
        script =    self.nodes[1].decoderawtransaction(rawTx)['vout'][0]['scriptPubKey']['asm']

        decodedRep = self.nodes[1].decoderawtransaction(rawTxReplaced)

        scriptRep = decodedRep['vout'][0]['scriptPubKey']['asm']
        print "  Changed script in tx"
        print "    from: ", script
        print "    to:   ", scriptRep

        signedRawTx         = self.nodes[1].signrawtransaction(rawTx)
        signedRawTxReplaced = self.nodes[1].signrawtransaction(rawTxReplaced)

        print "  | Node0 balance: ", self.nodes[0].getbalance()
        print "  | Node1 balance: ", self.nodes[1].getbalance()
        print "  | Node2 balance: ", self.nodes[2].getbalance()
        print "  | Node3 balance: ", self.nodes[3].getbalance()

        amountRep = Decimal('0')
        changeRep = Decimal('0')
        nAm = -1;
        nCh = -1;

        # get the amount and the change of the tx, they might not be ordered as vout entries
        for x in decodedRep['vout']:
            if x['value'] == PAYMENT:
                amountRep = x['value']
                nAm = x['n']
            else:
                changeRep = x['value']
                nCh = x['n']

        # the tx modified has a script that does not pass the check at referenced block, tx 1000 coins (and also related change)
        # will be unspendable once the chain is reverted
        tx_1000 = self.nodes[1].sendrawtransaction(signedRawTxReplaced['hex'])
        print "  ==> tx sent: ", tx_1000
        print "       amount (vout[%d]): %f" % (nAm, amountRep)
        print "       change (vout[%d]): %f" % (nCh, changeRep)

        sync_mempools(self.nodes[0:3])
        assert_equal(self.is_in_mempool(tx_1000, 1), True)
        print "  OK, tx is in mempool..."

        print "  | Node0 balance: ", self.nodes[0].getbalance()
        print "  | Node1 balance: ", self.nodes[1].getbalance()
        print "  | Node2 balance: ", self.nodes[2].getbalance()
        print "  | Node3 balance: ", self.nodes[3].getbalance()

        print("  Node0 generating 1 honest block")
        blocks.extend(self.nodes[0].generate(1))
        time.sleep(5)

        # check tx is no more in mempool
        assert_equal(self.is_in_mempool(tx_1000, 1), False)

        # check tx is in the block just mined
        assert_equal(self.is_in_block(tx_1000, blocks[-1], 1), True)
        print "  OK, tx is not in mempool anymore (it is contained in the block just mined)"
        print "  Block: (%d) %s" % ( int(self.nodes[2].getblockcount()), blocks[-1])

        print "  | Node0 balance: ", self.nodes[0].getbalance()
        print "  | Node1 balance: ", self.nodes[1].getbalance()
        print "  | Node2 balance: ", self.nodes[2].getbalance()
        print "  | Node3 balance: ", self.nodes[3].getbalance()

        print "  Node3 generating 3 malicious blocks thus reverting the honest chain once the ntw is joined!"
        blocks.extend(self.nodes[3].generate(3))
        time.sleep(2)

        self.join_network()

        time.sleep(2)
        print "  Network joined: (0)---(1)---(2)---(3)"
        self.mark_logs("Network joined")

        # check attacked transaction is back in the mempool of nodes belonging to the honest portion of the joined network
        assert_equal(self.is_in_mempool(tx_1000, 0), True)
        assert_equal(self.is_in_mempool(tx_1000, 1), True)
        assert_equal(self.is_in_mempool(tx_1000, 2), True)
        assert_equal(self.is_in_mempool(tx_1000, 3), False)

        print "  ==> the block has been reverted, the tx is back in the mempool of the reverted nodes!"

        print "  | Node0 balance: ", self.nodes[0].getbalance()
        print "  | Node1 balance: ", self.nodes[1].getbalance()
        print "  | Node2 balance: ", self.nodes[2].getbalance()
        print "  | Node3 balance: ", self.nodes[3].getbalance()

        print("  Node0 generating 1 honest block")
        blocks.extend(self.nodes[0].generate(1))
        time.sleep(2)

        # check tx_1000 is in the block just mined
        assert_equal(self.is_in_block(tx_1000, blocks[-1], 2), True)

        print "  ==> TX referencing reverted block has been mined in a new block!!"
        print "       Block: (%d) %s" % ( int(self.nodes[3].getblockcount()), blocks[-1])

        # restart Nodes and check their balance: node1 does not have 1000 coins but node2 does not have either
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False, 0)

        print "  Balances after node restart:"
        print "  | Node0 balance: ", self.nodes[0].getbalance()
        print "  | Node1 balance: ", self.nodes[1].getbalance()
        print "  | Node2 balance: ", self.nodes[2].getbalance()
        print "  | Node3 balance: ", self.nodes[3].getbalance()

        print "  Node3 generating %d honest blocks more" % (FINALITY_SAFE_DEPTH - 2)

        blocks.extend(self.nodes[3].generate(FINALITY_SAFE_DEPTH - 2))
        self.sync_all()

        # if safe depth is set to FINALITY_SAFE_DEPTH, from the previos block on the tx will become valid

        h_safe = self.nodes[1].getblockcount()

        assert_equal(h_safe, h_safe_estimated)

        print "  OK, at h[%d] the attacked tx should have been restored, node will have it after a restart" % h_safe

        node1_bal_before = Decimal(self.nodes[1].getbalance() )
        node2_bal_before = Decimal(self.nodes[2].getbalance() )

        print "  Balances before node restart:"
        print "  | Node0 balance: ", self.nodes[0].getbalance()
        print "  | Node1 balance: ", self.nodes[1].getbalance()
        print "  | Node2 balance: ", self.nodes[2].getbalance()
        print "  | Node2 balance: ", self.nodes[3].getbalance()

        # restart Nodes and check their balance, at this point the 1000 coins should be in the wallet of node2
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False, 0)

        node1_bal_after = Decimal(self.nodes[1].getbalance() )
        node2_bal_after = Decimal(self.nodes[2].getbalance() )

        print "  Balances after node restart:"
        print "  | Node0 balance: ", self.nodes[0].getbalance()
        print "  | Node1 balance: ", self.nodes[1].getbalance()
        print "  | Node2 balance: ", self.nodes[2].getbalance()
        print "  | Node2 balance: ", self.nodes[3].getbalance()

        # ensure both the amount (to the recipient) and the change (to the sender) have been restored
        assert_equal(node1_bal_before + changeRep, node1_bal_after)
        assert_equal(node2_bal_before + amountRep, node2_bal_after)

if __name__ == '__main__':
    headers().main()
