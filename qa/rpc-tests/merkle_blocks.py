#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test merkleblock fetch/validation
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_raises, \
    initialize_chain_clean, start_node, connect_nodes, \
    assert_true, assert_false, mark_logs, \
    advance_epoch, swap_bytes

from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.mc_test.mc_test import *

from decimal import Decimal
import pprint

DEBUG_MODE = 1
EPOCH_LENGTH = 6
CERT_FEE = Decimal('0.0001')

class MerkleBlockTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4)

    def setup_network(self):
        self.nodes = []
        # Nodes 0/1 are "wallet" nodes
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug"]))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-debug"]))
        # Nodes 2/3 are used for testing
        self.nodes.append(start_node(2, self.options.tmpdir, ["-debug"]))
        self.nodes.append(start_node(3, self.options.tmpdir, ["-debug", "-txindex"]))
        connect_nodes(self.nodes, 0, 1)
        connect_nodes(self.nodes, 0, 2)
        connect_nodes(self.nodes, 0, 3)

        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        print("Mining blocks...")
        self.nodes[0].generate(105)
        self.sync_all()

        chain_height = self.nodes[1].getblockcount()
        assert_equal(chain_height, 105)
        assert_equal(self.nodes[1].getbalance(), 0)
        assert_equal(self.nodes[2].getbalance(), 0)

        node0utxos = self.nodes[0].listunspent(1)
        tx1 = self.nodes[0].createrawtransaction([node0utxos.pop()], {self.nodes[1].getnewaddress(): 11.4375})
        txid1 = self.nodes[0].sendrawtransaction(self.nodes[0].signrawtransaction(tx1)["hex"])
        tx2 = self.nodes[0].createrawtransaction([node0utxos.pop()], {self.nodes[1].getnewaddress(): 11.4375})
        txid2 = self.nodes[0].sendrawtransaction(self.nodes[0].signrawtransaction(tx2)["hex"])
        assert_raises(JSONRPCException, self.nodes[0].gettxoutproof, [txid1])

        self.nodes[0].generate(1)
        blockhash = self.nodes[0].getblockhash(chain_height + 1)
        self.sync_all()

        txlist = []
        blocktxn = self.nodes[0].getblock(blockhash, True)["tx"]
        txlist.append(blocktxn[1])
        txlist.append(blocktxn[2])

        assert_equal(self.nodes[2].verifytxoutproof(self.nodes[2].gettxoutproof([txid1])), [txid1])
        assert_equal(self.nodes[2].verifytxoutproof(self.nodes[2].gettxoutproof([txid1, txid2])), txlist)
        assert_equal(self.nodes[2].verifytxoutproof(self.nodes[2].gettxoutproof([txid1, txid2], blockhash)), txlist)

        txin_spent = self.nodes[1].listunspent(1).pop()
        tx3 = self.nodes[1].createrawtransaction([txin_spent], {self.nodes[0].getnewaddress(): 11.4375})
        self.nodes[0].sendrawtransaction(self.nodes[1].signrawtransaction(tx3)["hex"])
        self.nodes[0].generate(1)
        self.sync_all()

        txid_spent = txin_spent["txid"]
        txid_unspent = txid1 if txin_spent["txid"] != txid1 else txid2

        # We cant find the block from a fully-spent tx
        assert_raises(JSONRPCException, self.nodes[2].gettxoutproof, [txid_spent])
        # ...but we can if we specify the block
        assert_equal(self.nodes[2].verifytxoutproof(self.nodes[2].gettxoutproof([txid_spent], blockhash)), [txid_spent])
        # ...or if the first tx is not fully-spent
        assert_equal(self.nodes[2].verifytxoutproof(self.nodes[2].gettxoutproof([txid_unspent])), [txid_unspent])
        try:
            assert_equal(self.nodes[2].verifytxoutproof(self.nodes[2].gettxoutproof([txid1, txid2])), txlist)
        except JSONRPCException:
            assert_equal(self.nodes[2].verifytxoutproof(self.nodes[2].gettxoutproof([txid2, txid1])), txlist)
        # ...or if we have a -txindex
        assert_equal(self.nodes[2].verifytxoutproof(self.nodes[3].gettxoutproof([txid_spent])), [txid_spent])

        # send funds to node 2, it will use them for sending a certificate
        tx0 = self.nodes[0].createrawtransaction([node0utxos.pop()], {self.nodes[2].getnewaddress(): 11.4375})
        self.nodes[0].sendrawtransaction(self.nodes[0].signrawtransaction(tx0)["hex"])

        # reach sc fork and create a SC
        self.nodes[0].generate(ForkHeights['MINIMAL_SC']-105)
        self.sync_all()
        prev_epoch_hash = self.nodes[0].getbestblockhash()

        sc_address = "0000000000000000000000000000000000000000000000000000000000000abc"
        sc_epoch_len = EPOCH_LENGTH
        sc_cr_amount = Decimal('12.00000000')

        certMcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        cswMcTest = CSWTestUtils(self.options.tmpdir, self.options.srcdir)

        # generate wCertVk and constant
        certVk = certMcTest.generate_params("sc")
        cswVk = cswMcTest.generate_params("sc")
        constant1 = generate_random_field_element_hex()

        sc_cr = []
        sc_cr.append({
            "version": 0,
            "epoch_length": sc_epoch_len,
            "amount": sc_cr_amount,
            "address": sc_address,
            "wCertVk": certVk,
            "wCeasedVk": cswVk,
            "constant": constant1
        })

        rawtx = self.nodes[0].createrawtransaction([], {}, [], sc_cr)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(finalRawtx, 1)
        scid = decoded_tx['vsc_ccout'][0]['scid']
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        # advance 1 epoch
        mark_logs("\nLet 1 epochs pass by...", self.nodes, DEBUG_MODE)
        q = 1
        cert_fee = Decimal("0.0")

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[2], self.sync_all,
            scid, "sc", constant1, sc_epoch_len, q, cert_fee)

        mark_logs("\n==> certificate for SC epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        assert_raises(JSONRPCException, self.nodes[0].gettxoutproof, [cert])

        # send some coin for having a tx in the next block as well
        t_addr1 = self.nodes[1].getnewaddress()
        tx = self.nodes[0].sendtoaddress(t_addr1, 0.1)
        self.sync_all()

        # mine one block for having last cert and tx in chain
        mark_logs("\nNode0 generates 1 block confirming last cert and tx", self.nodes, DEBUG_MODE)
        bl_hash = self.nodes[0].generate(1)[-1]
        self.sync_all()

        proof1 = self.nodes[1].gettxoutproof([cert])
        proof2 = self.nodes[2].gettxoutproof([tx, cert])

        assert_equal(self.nodes[2].verifytxoutproof(proof1), [cert])
        assert_equal(self.nodes[2].verifytxoutproof(proof2), [tx, cert])

        # spend cert change: since there are no bwts in the cert, it will be fully spent
        tx = self.nodes[2].sendtoaddress(t_addr1, 0.1)
        self.sync_all()

        mark_logs("\nNode0 generates 1 block confirming last tx", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # We cant find the block from a fully-spent cert
        assert_raises(JSONRPCException, self.nodes[0].gettxoutproof, [cert])
        # ...but we can if we specify the block
        proof = self.nodes[0].gettxoutproof([cert], bl_hash)
        # ...or if we have a -txindex
        proof = self.nodes[3].gettxoutproof([cert])

        assert_equal(self.nodes[0].verifytxoutproof(proof), [cert])
       
if __name__ == '__main__':
    MerkleBlockTest().main()
