#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.script import OP_DUP, OP_EQUALVERIFY, OP_HASH160, OP_EQUAL, hash160, OP_CHECKSIG, OP_CHECKBLOCKATHEIGHT
from test_framework.util import assert_equal, assert_greater_than, bytes_to_hex_str, initialize_chain_clean, \
    start_nodes, start_node, connect_nodes, stop_node, stop_nodes, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, p2p_port, check_json_precision, disconnect_nodes
from test_framework.script import CScript
from test_framework.mininode import CTransaction, ToHex
from test_framework.util import hex_str_to_bytes, swap_bytes
import traceback
from binascii import unhexlify
from io import BytesIO
import os,sys
import shutil
from decimal import Decimal
import binascii
import codecs
import pprint

import time

NUMB_OF_NODES = 4

# the cbh scripts will not be checked if we have more than this depth of referenced block
FINALITY_SAFE_DEPTH = 150

# 0 means do not check any minimum age for referenced blocks in scripts
FINALITY_MIN_AGE = 75 

# mainchain uses this value for targeting a blockhash when building cbh script starting from chain tip backwards
CBH_DELTA_HEIGHT = 300

class checkblockatheight(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False, minAge=FINALITY_MIN_AGE):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
            extra_args = [
                ["-debug=py", "-debug=cbh", "-cbhsafedepth="+str(FINALITY_SAFE_DEPTH), "-cbhminage="+str(minAge)],
                ["-debug=py", "-debug=cbh", "-cbhsafedepth="+str(FINALITY_SAFE_DEPTH), "-cbhminage="+str(minAge)],
                ["-debug=py", "-debug=cbh", "-cbhsafedepth="+str(FINALITY_SAFE_DEPTH), "-cbhminage="+str(minAge)],
                ["-debug=py", "-debug=cbh", "-cbhsafedepth="+str(FINALITY_SAFE_DEPTH), "-cbhminage="+str(minAge)]
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

    def join_network(self):
        #Join the (previously split) network pieces together: 0-1-2-3
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 2)
        sync_blocks(self.nodes, 1, False, 5)
        self.is_network_split = False

    def mark_logs(self, msg):
        print(msg)
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)
        self.nodes[2].dbg_log(msg)
        self.nodes[3].dbg_log(msg)

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

        blocks.append(self.nodes[0].getblockhash(0))

        small_target_h = 3

        self.mark_logs("Node0 generates %d blocks" % (CBH_DELTA_HEIGHT + small_target_h - 1))
        blocks.extend(self.nodes[0].generate(CBH_DELTA_HEIGHT + small_target_h -1))
        self.sync_all()

        amount_node1 = Decimal("1002.0")
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), amount_node1)
        self.sync_all()

        self.mark_logs("Node0 generates 1 blocks")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        self.mark_logs("Trying to send a tx with a scriptPubKey referencing a block too recent...")

        # Create a tx having in its scriptPubKey a custom referenced block in the CHECKBLOCKATHEIGHT part 
        # select necessary utxos for doing the PAYMENT
        usp = self.nodes[0].listunspent()

        payment = Decimal('1.0')
        fee     = Decimal('0.00005')

        amount  = Decimal('0')
        inputs  = []
        for x in usp:
            amount += Decimal(x['amount']) 
            inputs.append( {"txid":x['txid'], "vout":x['vout']})
            if amount >= payment+fee:
                break

        outputs = {self.nodes[0].getnewaddress(): (Decimal(amount) - payment - fee), self.nodes[2].getnewaddress(): payment}
        rawTx   = self.nodes[0].createrawtransaction(inputs, outputs)

        # build an object from the raw tx in order to be able to modify it
        tx_01 = CTransaction()
        f = BytesIO(unhexlify(rawTx))
        tx_01.deserialize(f)

        decodedScriptOrig = self.nodes[0].decodescript(bytes_to_hex_str(tx_01.vout[1].scriptPubKey))

        scriptOrigAsm = decodedScriptOrig['asm']

        # store the hashed script, it is reused
        params = scriptOrigAsm.split()
        hash160 = hex_str_to_bytes(params[2])

        # new referenced block height
        modTargetHeigth = CBH_DELTA_HEIGHT + small_target_h - FINALITY_MIN_AGE + 5

        # new referenced block hash
        modTargetHash = hex_str_to_bytes(swap_bytes(blocks[modTargetHeigth]))
        
        # build modified script
        modScriptPubKey = CScript([OP_DUP, OP_HASH160, hash160, OP_EQUALVERIFY, OP_CHECKSIG, modTargetHash, modTargetHeigth, OP_CHECKBLOCKATHEIGHT])

        tx_01.vout[1].scriptPubKey = modScriptPubKey
        tx_01.rehash()

        decodedScriptMod = self.nodes[0].decodescript(bytes_to_hex_str(tx_01.vout[1].scriptPubKey))
        print("  Modified scriptPubKey in tx 1: ", decodedScriptMod['asm'])

        signedRawTx = self.nodes[0].signrawtransaction(ToHex(tx_01))

        h = self.nodes[0].getblockcount()
        assert_greater_than(FINALITY_MIN_AGE, h - modTargetHeigth)

        print("  Node0 sends %f coins to Node2" % payment)

        try:
            txid = self.nodes[0].sendrawtransaction(signedRawTx['hex'])
            print("  Tx sent: ", txid)
            # should fail, therefore force test failure
            assert_equal(True, False)

        except JSONRPCException as e:
            print("  ==> tx has been rejected as expected:")
            print("      referenced block height=%d, chainActive.height=%d, minimumAge=%d" % (modTargetHeigth, h, FINALITY_MIN_AGE))
            print

        #-------------------------------------------------------------------------------------------------------
        print("Check that small digit height for referenced blocks works in scriptPubKey")
        #-------------------------------------------------------------------------------------------------------
        # check that small height works for 'check block at height' in script
        #--------------------------------------------------------------------
        node1_pay = Decimal('0.5')
        self.mark_logs("  Node0 sends %f coins to Node3" % node1_pay)

        tx1 = self.nodes[0].sendtoaddress(self.nodes[3].getnewaddress(), node1_pay)
        print("  ==> tx sent: ", tx1)

        sync_mempools(self.nodes[0:4])

        assert_equal(self.is_in_mempool(tx1), True)

        script = self.nodes[0].getrawtransaction(tx1, 1)['vout'][0]['scriptPubKey']['asm']
        tokens = script.split()
        small_h = int(tokens[6])
        small_h_hash = swap_bytes(tokens[5])

        print("  ScriptPubKey: ", script)

        assert_equal(small_h, small_target_h)
        assert_equal(small_h_hash, blocks[int(small_h)])

        print("  Node0 generating 1 honest block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        print("  | Node2 balance: ", self.nodes[2].getbalance())
        print("  | Node3 balance: ", self.nodes[3].getbalance())

        # check tx is no more in mempool
        assert_equal(self.is_in_mempool(tx1), False)

        # check tx is in the block just mined
        assert_equal(self.is_in_block(tx1, blocks[-1], 3), True)

        # check the balance is the one expected
        assert_equal(self.nodes[3].getbalance(), node1_pay)

        amount_node2 = Decimal("0.25")
        self.mark_logs("  Node3 sends {} coins to Node2".format(amount_node2) )

        tx2 = self.nodes[3].sendtoaddress(self.nodes[2].getnewaddress(), amount_node2)
        print("  ==> tx sent: ", tx2)

        sync_mempools(self.nodes[0:4])

        assert_equal(self.is_in_mempool(tx2), True)

        print("  Node0 generating 1 honest block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        # check tx is no more in mempool
        assert_equal(self.is_in_mempool(tx2), False)

        # check tx is in the block just mined
        assert_equal(self.is_in_block(tx2, blocks[-1], 1), True)

        # check Node2 got his money
        assert_equal(amount_node2, self.nodes[2].getbalance())

        print("  ==> OK, small digit height works in scripts:")
        print
        #--------------------------------------------------------------------
        
        print("Verify that a legal tx, when the referenced block is reverted, becomes invalid until the necessary depth of the referenced block height has been reached...")
        # it is necessary to use cbhminage=0 otherwise the tx to be setup would be refused since it refers
        # to a block too recent in cbh script 
        print("  Restarting network with -cbhminage=0")

        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False, 0)

        chunks = 305
        self.mark_logs("  Node0 generates %d blocks" % chunks)
        blocks.extend(self.nodes[0].generate(chunks)) 
        self.sync_all()

        self.mark_logs("  Split network: (0)---(1)---(2)   (3)")
        self.split_network(2)

        self.mark_logs("  Node0 generating 1 honest block")
        blocks.extend(self.nodes[0].generate(1)) 
        sync_blocks(self.nodes, 1, False, 5)

        h_current = self.nodes[1].getblockcount()

        # we will perform on attack aimed at reverting from this block (latest generated) upward
        h_attacked = h_current
        hash_attacked = blocks[-1]
        assert hash_attacked == blocks[h_attacked]
        hash_attacked_swapped = swap_bytes(hash_attacked)
        hex_tmp = "%04x" % h_attacked
        h_attacked_swapped = swap_bytes(hex_tmp)
        h_safe_estimated = h_attacked + FINALITY_SAFE_DEPTH + 1

        print("  Honest network has current h[%d]" % h_attacked)

        # mainchain creates cbh scripts where target block hash/height are CBH_DELTA_HEIGHT older than chain tip
        h_checked = h_current - CBH_DELTA_HEIGHT
        hex_tmp = "%04x" % h_checked
        h_checked_swapped = swap_bytes(hex_tmp)
        hash_checked = blocks[h_checked]
        hash_checked_swapped = swap_bytes(hash_checked)

        # select input, we have just one big utxo 
        usp = self.nodes[1].listunspent()
        assert_equal(len(usp), 1)

        amount = Decimal('1000.0')
        fee    = Decimal('0.00005')
        change = amount_node1 - amount - fee

        inputs  = [{"txid":usp[0]['txid'], "vout":usp[0]['vout']}]
        outputs = {self.nodes[1].getnewaddress(): change, self.nodes[2].getnewaddress(): amount}

        print("  Creating raw tx referencing the current block %d where Node1 sends %f coins to Node2..." % (h_attacked, amount))
        rawTx   = self.nodes[1].createrawtransaction(inputs, outputs)

        # replace hash and h referenced in tx's script with the ones of target block 
        from_buf = str(hash_checked_swapped)+'02'+str(h_checked_swapped)
        to_buf   = str(hash_attacked_swapped)+'02'+str(h_attacked_swapped)

        # tamper just the output to Node2, not the change

        # select the right output, we can not rely on order of appearence of outputs
        decVout = self.nodes[1].decoderawtransaction(rawTx)['vout']
        idx = 0
        if decVout[1]['value'] == amount:
            idx = 1

        sp1 = decVout[idx]['scriptPubKey']['hex']
        sp2 = sp1.replace(from_buf, to_buf)

        rawTxReplaced = rawTx.replace(sp1, sp2)

        signedRawTxReplaced = self.nodes[1].signrawtransaction(rawTxReplaced)

        print("  | Node1 balance: ", self.nodes[1].getbalance())
        print("  | Node2 balance: ", self.nodes[2].getbalance())
        assert_equal(amount_node1, self.nodes[1].getbalance())
        assert_equal(amount_node2, self.nodes[2].getbalance())

        # the tx modified has a script that is currently valid but that will not pass the check
        # at referenced block once the chain is reverted
        tx_1000 = self.nodes[1].sendrawtransaction(signedRawTxReplaced['hex'])
        print("  ==> tx sent: ", tx_1000)
        print("       amount : %f" % ( amount))
        print("       change : %f" % ( change))

        sync_mempools(self.nodes[0:3])
        assert_equal(self.is_in_mempool(tx_1000, 1), True)
        print("  OK, tx is in mempool...")
        print("  | Node1 balance: ", self.nodes[1].getbalance())
        print("  | Node2 balance: ", self.nodes[2].getbalance())
        assert_equal(change,    self.nodes[1].getbalance())
        assert_equal(amount_node2, self.nodes[2].getbalance())

        print("  Node0 generating 1 honest block")
        blocks.extend(self.nodes[0].generate(1))
        sync_blocks(self.nodes[0:2])
        
        # check tx is no more in mempool
        assert_equal(self.is_in_mempool(tx_1000, 1), False)

        # check tx is in the block just mined
        assert_equal(self.is_in_block(tx_1000, blocks[-1], 1), True)
        print("  OK, tx is not in mempool anymore (it is contained in the block just mined)")
        print("  Block: (%d) %s" % ( int(self.nodes[2].getblockcount()), blocks[-1]))

        print("  | Node1 balance: ", self.nodes[1].getbalance())
        print("  | Node2 balance: ", self.nodes[2].getbalance())
        assert_equal(change, self.nodes[1].getbalance())
        assert_equal(amount_node2 + amount, self.nodes[2].getbalance())

        print("  Node3 generating 3 malicious blocks thus reverting the honest chain once the ntw is joined!")
        blocks.extend(self.nodes[3].generate(3))
        sync_blocks(self.nodes, limit_loop=5)
        
        self.mark_logs("Joining network")
        self.join_network()

        print("  Network joined: (0)---(1)---(2)---(3)")
        self.mark_logs("Network joined")

        # check attacked transaction is back in the mempool of nodes belonging to the honest portion of the joined network
        assert_equal(self.is_in_mempool(tx_1000, 0), True)
        assert_equal(self.is_in_mempool(tx_1000, 1), True)
        assert_equal(self.is_in_mempool(tx_1000, 2), True)
        assert_equal(self.is_in_mempool(tx_1000, 3), False)

        print("  ==> the block has been reverted, the tx is back in the mempool of the reverted nodes!")

        node1_bal = Decimal(self.nodes[1].getbalance() )
        node2_bal = Decimal(self.nodes[2].getbalance() )

        print("  | Node1 balance: ", node1_bal)
        print("  | Node2 balance: ", node2_bal)
        assert_equal(change, node1_bal)
        assert_equal(amount_node2, node2_bal)

        print("  Node0 generating 1 honest block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        
        # check tx_1000 is in the block just mined
        assert_equal(self.is_in_block(tx_1000, blocks[-1], 2), True)

        print("  ==> TX referencing reverted block has been mined in a new block!!")
        print("       Block: (%d) %s" % ( int(self.nodes[3].getblockcount()), blocks[-1]))

        node1_bal = Decimal(self.nodes[1].getbalance() )
        node2_bal = Decimal(self.nodes[2].getbalance() )

        print("  | Node1 balance: ", node1_bal)
        print("  | Node2 balance: ", node2_bal)
        assert_equal(change, node1_bal)
        assert_equal(amount_node2, node2_bal)

        print("  Node3 generating %d honest blocks more" % (FINALITY_SAFE_DEPTH - 2))

        blocks.extend(self.nodes[3].generate(FINALITY_SAFE_DEPTH - 2))
        self.sync_all()

        # from this block on, the tx will become valid because finality safe depth has been reached
        # and the cbh script is not checked anymore
        h_safe = self.nodes[1].getblockcount()
        assert_equal(h_safe, h_safe_estimated)

        node1_zbal = Decimal(self.nodes[1].z_gettotalbalance()['total'])
        node2_zbal = Decimal(self.nodes[2].z_gettotalbalance()['total'])
        node1_bal = Decimal(self.nodes[1].getbalance() )
        node2_bal = Decimal(self.nodes[2].getbalance() )

        print("  OK, at h[%d] the attacked tx have been restored (node will have it in cached balance after a restart)" % h_safe)

        print("  Balances before node restart:")
        print("  | Node1 balance (cached): ", node1_bal)
        print("  | Node2 balance (cached): ", node2_bal)
        print("  | Node1 z_balance: ", node1_zbal)
        print("  | Node2 z_balance: ", node2_zbal)

        # non-cached balance must be ok while cached balance still does not have correct amounts
        assert_equal(node2_bal + amount, node2_zbal)


        # restart Nodes and check their balance, at this point the 1000 coins should be in the wallet of node2
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False, 0)

        final_bal1 = Decimal(self.nodes[1].getbalance() )
        final_bal2 = Decimal(self.nodes[2].getbalance() )
        node1_zbal = Decimal(self.nodes[1].z_gettotalbalance()['total'])
        node2_zbal = Decimal(self.nodes[2].z_gettotalbalance()['total'])

        print("  Balances after node restart:")
        print("  | Node1 balance: ", final_bal1)
        print("  | Node2 balance: ", final_bal2)

        # ensure the amounts have been restored in cached balances
        assert_equal(node1_bal, final_bal1)
        assert_equal(node2_bal + amount, final_bal2)
        assert_equal(node1_zbal, final_bal1)
        assert_equal(node2_zbal, final_bal2)


if __name__ == '__main__':
    checkblockatheight().main()
