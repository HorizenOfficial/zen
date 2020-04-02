#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, dump_ordered_tips
import os
from decimal import Decimal
import pprint

DEBUG_MODE = 1
NUMB_OF_NODES = 3
EPOCH_LENGTH = 5
CERT_FEE = 0.0001


class sc_cr_fw(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=bench', '-debug=net', '-debug=cert', '-logtimemicros=1']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        Test the situation when bot a fw transfer and a certificate for the same scid are in the mempool and a block is mined"
        '''

        def get_epoch_data(node, sc_creating_height, epoch_length):
            current_height = node.getblockcount()
            epoch_number = (current_height - sc_creating_height + 1) // epoch_length - 1
            epoch_block_hash = node.getblockhash(sc_creating_height - 1 + ((epoch_number + 1) * epoch_length))
            return epoch_number, epoch_block_hash

        # side chain id
        scid = "1111111111111111111111111111111111111111111111111111111111111111"

        # forward transfer amounts
        creation_amount = Decimal("4.99978")
        fwt_amount = Decimal("1365.0")
        bwt_amount = Decimal("0.03")

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates 220 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(220)
        self.sync_all()

        totScAmount = 0
        # sidechain creation
        #-------------------
        creating_tx = self.nodes[1].sc_create(scid, EPOCH_LENGTH, "dada", creation_amount, "abcdef")
        mark_logs("Node 1 created a sidechain via {}".format(creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        totScAmount += creation_amount

        mark_logs("Node 0 sends to sidechain ", self.nodes, DEBUG_MODE)
        txes = []
        for i in range(1,10):
            amounts = []
            interm_amount = 0
            for j in range(1,10):
                scaddr = str(hex(j*i)) 
                amount = j*i*Decimal('0.01')
                interm_amount += amount
                amounts.append({"address": scaddr, "amount": amount, "scid": scid})
            txes.append(self.nodes[0].sc_sendmany(amounts))
            mark_logs("Node 0 send many amounts (tot={}) to sidechain via {}".format(interm_amount, txes[-1]), self.nodes, DEBUG_MODE)
            self.sync_all()
            totScAmount += interm_amount


        mark_logs("Check creation tx is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, creating_tx in self.nodes[1].getrawmempool())

        count = 0
        for txm in self.nodes[1].getrawmempool():
            count += 1
            if (creating_tx == txm):
                mark_logs("creation tx is at position {} in mempool".format(count), self.nodes, DEBUG_MODE)
                break

        mark_logs("Check fw txes are in mempools", self.nodes, DEBUG_MODE)
        for fwt in txes:
            assert_equal(True, fwt in self.nodes[1].getrawmempool())

        mark_logs("Node1 generates 1 block", self.nodes, DEBUG_MODE)
        bl_hash = self.nodes[1].generate(1)[0]
        self.sync_all()

        mark_logs("Check mempools are empty", self.nodes, DEBUG_MODE)
        assert_equal(0, len(self.nodes[0].getrawmempool()))

        mark_logs("Check that all fw txes are in block", self.nodes, DEBUG_MODE)
        block = self.nodes[0].getblock(bl_hash, True)
        vtx   = block['tx']
        for fwt in txes:
            assert_equal(True, fwt in vtx)

        mark_logs("Check that creating tx is the second in list (after coinbase)", self.nodes, DEBUG_MODE)
        assert_equal(creating_tx, vtx[1])

        mark_logs("Node 0 invalidates last block", self.nodes, DEBUG_MODE)
        self.nodes[0].invalidateblock(bl_hash)

        mark_logs("Check creation tx is in mempool", self.nodes, DEBUG_MODE)
        assert_equal(True, creating_tx in self.nodes[0].getrawmempool())

        count = 0
        for txm in self.nodes[0].getrawmempool():
            count += 1
            if (creating_tx == txm):
                mark_logs("creation tx is at position {} in mempool".format(count), self.nodes, DEBUG_MODE)
                break

        mark_logs("Check fw txes are in mempool", self.nodes, DEBUG_MODE)
        for fwt in txes:
            assert_equal(True, fwt in self.nodes[0].getrawmempool())

        mark_logs("Node0 generates 1 block", self.nodes, DEBUG_MODE)
        bl_hash = self.nodes[0].generate(1)[0]
        self.sync_all()

        mark_logs("Check mempool is empty", self.nodes, DEBUG_MODE)
        assert_equal(0, len(self.nodes[0].getrawmempool()))

        mark_logs("Check that all fw txes are in block", self.nodes, DEBUG_MODE)
        block = self.nodes[0].getblock(bl_hash, True)
        vtx   = block['tx']
        for fwt in txes:
            assert_equal(True, fwt in vtx)

        mark_logs("Check that creating tx is the second in list (after coinbase)", self.nodes, DEBUG_MODE)
        assert_equal(creating_tx, vtx[1])

        mark_logs("Node0 generates 5 more blocks", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()

        mark_logs("Check that sc balance is as expected", self.nodes, DEBUG_MODE)
        pprint.pprint(self.nodes[1].getscinfo(scid))
        assert_equal(totScAmount, self.nodes[1].getscinfo(scid)['balance'])
        mark_logs("Check that both nodes share the same view of sc info", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[0].getscinfo(scid), self.nodes[1].getscinfo(scid))


if __name__ == '__main__':
    sc_cr_fw().main()
