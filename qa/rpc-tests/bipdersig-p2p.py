#!/usr/bin/env python2
#
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#

from test_framework.test_framework import ComparisonTestFramework
from test_framework.util import start_nodes
from test_framework.mininode import CTransaction, NetworkThread
from test_framework.blocktools import create_coinbase, create_block
from test_framework.comptool import TestInstance, TestManager
from test_framework.script import CScript
from binascii import unhexlify
import cStringIO
import time


'''
This test is meant to exercise BIP66 (DER SIG).
Connect to a single node.
Mine a coinbase block, and then ...
Mine 1 version 4 block.
Check that the DERSIG rules are enforced.

TODO: factor out common code from {bipdersig-p2p,bip65-cltv-p2p}.py.
'''
class BIP66Test(ComparisonTestFramework):

    def __init__(self):
        self.num_nodes = 1

    def setup_network(self):
        self.nodes = start_nodes(1, self.options.tmpdir,
                                 extra_args=[['-debug', '-whitelist=127.0.0.1']],
                                 binary=[self.options.testbinary])
        self.is_network_split = False

    def run_test(self):
        test = TestManager(self, self.options.tmpdir)
        # Don't call test.add_all_connections because there is only one node.
        NetworkThread().start() # Start up network handling in another thread
        test.run()

    def create_transaction(self, node, coinbase, to_address, amount):
        from_txid = node.getblock(coinbase)['tx'][0]
        inputs = [{ "txid" : from_txid, "vout" : 0}]
        outputs = { to_address : amount }
        rawtx = node.createrawtransaction(inputs, outputs)
        signresult = node.signrawtransaction(rawtx)
        tx = CTransaction()
        f = cStringIO.StringIO(unhexlify(signresult['hex']))
        tx.deserialize(f)
        return tx

    def invalidate_transaction(self, tx):
        '''
        Make the signature in vin 0 of a tx non-DER-compliant,
        by adding padding after the S-value.

        A canonical signature consists of:
        <30> <total len> <02> <len R> <R> <02> <len S> <S> <hashtype>
        '''
        scriptSig = CScript(tx.vin[0].scriptSig)
        newscript = []
        for i in scriptSig:
            if (len(newscript) == 0):
                newscript.append(i[0:-1] + '\0' + i[-1])
            else:
                newscript.append(i)
        tx.vin[0].scriptSig = CScript(newscript)

    def get_tests(self):
        self.coinbase_blocks = self.nodes[0].generate(1)
        self.tip = int ("0x" + self.nodes[0].getbestblockhash() + "L", 0)
        self.nodeaddress = self.nodes[0].getnewaddress()
        self.block_time = time.time() + 1

        '''Check that the rules are enforced.'''
        for valid in (True, False):
            spendtx = self.create_transaction(self.nodes[0],
                                              self.coinbase_blocks[0],
                                              self.nodeaddress, 1.0)
            if not valid:
                self.invalidate_transaction(spendtx)
                spendtx.rehash()

            block = create_block(self.tip, create_coinbase(1), self.block_time)
            block.nVersion = 4
            block.vtx.append(spendtx)
            block.hashMerkleRoot = block.calc_merkle_root()
            block.rehash()
            block.solve()
            self.block_time += 1
            self.tip = block.sha256
            yield TestInstance([[block, valid]])


if __name__ == '__main__':
    BIP66Test().main()
