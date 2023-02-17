#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_equal, initialize_chain_clean, \
    start_nodes, stop_nodes, wait_bitcoinds, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, dump_ordered_tips
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal
import pprint

DEBUG_MODE = 1
NUMB_OF_NODES = 3
EPOCH_LENGTH = 5
CERT_FEE = 0.0001
BUNCH_SIZE=5


class sc_cr_fw(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False, bool_flag=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-deprecatedgetblocktemplate=%d'%bool_flag, '-debug=cert', '-logtimemicros=1']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        Test the situation when both a fw transfer and a certificate for the same scid are in the mempool and a block is mined"
        '''

        def get_epoch_data(node, sc_creating_height, epoch_length):
            current_height = node.getblockcount()
            epoch_number = (current_height - sc_creating_height + 1) // epoch_length - 1
            epoch_block_hash = node.getblockhash(sc_creating_height - 1 + ((epoch_number + 1) * epoch_length))
            return epoch_number, epoch_block_hash

        # forward transfer amounts
        creation_amount = Decimal(MINER_REWARD_POST_H200*(ForkHeights['MINIMAL_SC']-100)) #Most of mature coins owned by Node
        fwt_amount = Decimal("3.0")

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()

        # generate a tx in mempool whose coins will be used by the tx creating the sc as input. This will make the creation tx orphan
        # and with null prio (that is because its inputs have 0 conf). As a consequence it would be processed after the forward transfer, making the block invalid.
        # Handling sc dependancies will prevent this scenario.
        tx = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), creation_amount);
        mark_logs("Node 0 sent {} coins to itself via {}".format(creation_amount, tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        totScAmount = 0
        # sidechain creation
        #-------------------
        # generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": constant,
            "minconf": 0
        }

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        self.sync_all()
        mark_logs("Node0 created SC scid={}, tx={}".format(scid, creating_tx), self.nodes, DEBUG_MODE)

        totScAmount += creation_amount

        mark_logs("Node 0 sends to sidechain ", self.nodes, DEBUG_MODE)
        mc_return_address = self.nodes[0].getnewaddress()
        txes = []
        for i in range(1, BUNCH_SIZE+1):
            amounts = []
            interm_amount = 0
            for j in range(1, BUNCH_SIZE+1):
                scaddr = u'{0:0{1}x}'.format(j*i, 4)
                amount = j*i*Decimal('0.01')
                interm_amount += amount
                amounts.append({"toaddress": scaddr, "amount": amount, "scid": scid, "mcReturnAddress": mc_return_address})
            cmdParams = {"minconf": 0}
            txes.append(self.nodes[0].sc_send(amounts, cmdParams))
            mark_logs("Node 0 send many amounts (tot={}) to sidechain via {}".format(interm_amount, txes[-1]), self.nodes, DEBUG_MODE)
            self.sync_all()
            totScAmount += interm_amount


        mark_logs("Check creation tx is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, creating_tx in self.nodes[1].getrawmempool())

        # pprint.pprint(self.nodes[1].getrawmempool(True))

        mp = self.nodes[1].getrawmempool(True)
        prio_cr_tx = mp[creating_tx]['currentpriority']
        dep_cr_tx  = mp[creating_tx]['depends'][0]

        mark_logs("creation tx prio {}".format(prio_cr_tx), self.nodes, DEBUG_MODE)
        # assert_equal(0, prio_cr_tx)
        mark_logs("creation tx depends on {}".format(dep_cr_tx), self.nodes, DEBUG_MODE)
        assert_equal(tx, dep_cr_tx)

        mark_logs("Check fw txes are in mempools and their prio is greater than sc creation prio", self.nodes, DEBUG_MODE)
        for fwt in txes:
            assert_equal(True, fwt in self.nodes[1].getrawmempool())
            prio_fwt = mp[fwt]['currentpriority']
            dep_fwt  = mp[fwt]['depends'][-1]
            assert_true(prio_fwt < prio_cr_tx)
            assert_equal(dep_fwt, creating_tx)


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

        mark_logs("Check that creating tx is the third in list (after coinbase)", self.nodes, DEBUG_MODE)
        assert_equal(creating_tx, vtx[2])


        #-------------------------------------------------------
        mark_logs("stopping and restarting nodes with '-deprecatedgetblocktemplate=1'", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False, True)

        mark_logs("Node 0 invalidates last block", self.nodes, DEBUG_MODE)
        self.nodes[0].invalidateblock(bl_hash)

        mark_logs("Check creation tx is in mempool", self.nodes, DEBUG_MODE)
        assert_equal(True, creating_tx in self.nodes[0].getrawmempool())

        mark_logs("Check fw txes are in mempool", self.nodes, DEBUG_MODE)
        for fwt in txes:
            assert_equal(True, fwt in self.nodes[0].getrawmempool())

        mp = self.nodes[0].getrawmempool(True)
        prio_cr_tx = mp[creating_tx]['currentpriority']
        dep_cr_tx  = mp[creating_tx]['depends'][0]

        mark_logs("creation tx prio {}".format(prio_cr_tx), self.nodes, DEBUG_MODE)
        #assert_equal(0, prio_cr_tx) TODO lower prio via rpc
        mark_logs("creation tx depends on {}".format(dep_cr_tx), self.nodes, DEBUG_MODE)
        assert_equal(tx, dep_cr_tx)

        mark_logs("Check fw txes are in mempools and their prio is greater than sc creation prio", self.nodes, DEBUG_MODE)
        for fwt in txes:
            assert_equal(True, fwt in self.nodes[0].getrawmempool())
            prio_fwt = mp[fwt]['currentpriority']
            dep_fwt  = mp[fwt]['depends'][-1]
            #assert_true(prio_fwt > prio_cr_tx) TODO see above
            assert_equal(dep_fwt, creating_tx)

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

        mark_logs("Check that creating tx is the third in list (after coinbase)", self.nodes, DEBUG_MODE)
        assert_equal(creating_tx, vtx[2])

        mark_logs("Node0 generates 5 more blocks", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()

        mark_logs("Check that sc balance is as expected", self.nodes, DEBUG_MODE)
        pprint.pprint(self.nodes[1].getscinfo(scid))
        assert_equal(totScAmount, self.nodes[1].getscinfo(scid)['items'][0]['balance'])
        mark_logs("Check that both nodes share the same view of sc info", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[0].getscinfo(scid), self.nodes[1].getscinfo(scid))


if __name__ == '__main__':
    sc_cr_fw().main()
