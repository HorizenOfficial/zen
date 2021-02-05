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
    start_nodes, connect_nodes_bi, assert_true, assert_false, mark_logs, \
    wait_bitcoinds, stop_nodes, get_epoch_data, sync_mempools, sync_blocks, \
    disconnect_nodes, advance_epoch

from test_framework.mc_test.mc_test import *

from decimal import Decimal
import pprint
import time

NUMB_OF_NODES = 3
DEBUG_MODE = 1
EPOCH_LENGTH = 10

# Create one-input, one-output, no-fee transaction:
class CeasingSplitTest(BitcoinTestFramework):

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
        Create a SC, advance two epochs and then 
        Test CSW txes and related in/outputs verifying that nullifiers are properly handled also
        when blocks are disconnected.
        Split the network and test CSW conflicts handling on network rejoining.
        Restare the network and check DB integrity.
        Finally create a second SC, advance 1 epoch only and verify that no CSW can be accepted (max is 2 certs)
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
        vk = mcTest.generate_params("sc1")
        cswVk = mcTest.generate_params("csw1")
        constant = generate_random_field_element_hex()

        sc_cr = []
        sc_cr.append({
            "epoch_length": sc_epoch_len,
            "amount": sc_cr_amount,
            "address": sc_address,
            "wCertVk": vk,
            "wCeasedVk": cswVk,
            "constant": constant
        })

        rawtx = self.nodes[0].createrawtransaction([], {}, [], sc_cr, [])
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        self.sync_all()

        decoded_tx = self.nodes[2].getrawtransaction(finalRawtx, 1)
        scid = decoded_tx['vsc_ccout'][0]['scid']
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        # advance two epochs
        mark_logs("\nLet 2 epochs pass by...".  format(sc_epoch_len), self.nodes, DEBUG_MODE)

        cert, epoch_block_hash, epoch_number = advance_epoch(
            mcTest, self.nodes[0], self.sync_all,
             scid, prev_epoch_hash, "sc1", constant, sc_epoch_len)

        mark_logs("\n==> certificate for epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        prev_epoch_hash = epoch_block_hash

        cert, epoch_block_hash, epoch_number = advance_epoch(
            mcTest, self.nodes[0], self.sync_all,
             scid, prev_epoch_hash, "sc1", constant, sc_epoch_len)

        mark_logs("\n==> certificate for epoch {} {}l".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        ceas_height = self.nodes[0].getscinfo(scid, False, False)['items'][0]['ceasing height']
        numbBlocks = ceas_height - self.nodes[0].getblockcount() + sc_epoch_len - 1

        mark_logs("\nNode0 generates {} block reaching the sg for the next epoch".format(numbBlocks), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(numbBlocks)
        self.sync_all()



        #============================================================================================
        mark_logs("\nSplit network", self.nodes, DEBUG_MODE)
        self.split_network()
        mark_logs("The network is split: 0-1 .. 2", self.nodes, DEBUG_MODE)

        # Network part 0-1
        #------------------
        fwt_amount = Decimal("2.0")
        mark_logs("\nNode0 sends {} coins to SC".format(fwt_amount), self.nodes, DEBUG_MODE)
        tx_fwd = self.nodes[0].sc_send("abcd", fwt_amount, scid)
        self.sync_all()

        mark_logs("Check fwd tx {} is in mempool".format(tx_fwd), self.nodes, DEBUG_MODE)
        assert_true(tx_fwd in self.nodes[0].getrawmempool()) 

        # Network part 2
        #------------------

        mark_logs("Let SC cease... ".format(scid), self.nodes, DEBUG_MODE)

        mark_logs("Node0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[2].generate(1)
        self.sync_all()

        # check it is really ceased
        ret = self.nodes[2].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")

        #============================================================================================
        mark_logs("\nJoining network", self.nodes, DEBUG_MODE)
        self.join_network()
        mark_logs("Network joined", self.nodes, DEBUG_MODE)

        mark_logs("Check tx {} is no more in mempool".format(tx_fwd), self.nodes, DEBUG_MODE)
        assert_false(tx_fwd in self.nodes[0].getrawmempool()) 

        pprint.pprint(self.nodes[0].getscinfo(scid, False, True)['items'][0]['balance'])


if __name__ == '__main__':
    CeasingSplitTest().main()
