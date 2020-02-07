#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, p2p_port, mark_logs, disconnect_nodes
import os
from decimal import Decimal
import time

NUMB_OF_NODES = 3
DEBUG_MODE = 0


class ScSplitTest(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[['-sccoinsmaturity=0', '-logtimemicros=1', '-debug=sc', '-debug=py',
                                              '-debug=mempool', '-debug=net', '-debug=bench']] * NUMB_OF_NODES)

        if not split:
            # 1 and 2 are joint only if split==false
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:NUMB_OF_NODES])
            sync_mempools(self.nodes[1:NUMB_OF_NODES])

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
        This test creates a Sidechain and forwards funds to it and then verifies
        that scinfo is updated correctly in active chain also after alternative fork propagations
        '''
        # network topology: (0)--(1)--(2)

        # side chain id
        scid = "22"

        # forward transfer amount
        creation_amount = Decimal("0.5")
        fwt_amount_1 = Decimal("4.0")
        fwt_amount_2 = Decimal("1.0")

        blocks = [self.nodes[0].getblockhash(0)]

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block",self.nodes,DEBUG_MODE)

        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        mark_logs("Node 0 generates 220 block",self.nodes,DEBUG_MODE)

        blocks.extend(self.nodes[0].generate(220))
        self.sync_all()

        # Split the network: (0)--(1) / (2)
        mark_logs("\nSplit network",self.nodes,DEBUG_MODE)
        self.split_network()
        mark_logs("The network is split: 0-1 .. 2",self.nodes,DEBUG_MODE)

        txes = []

        # Nodes 1 send 5.0 coins to a valid taddr to verify the network split
        mark_logs("\nNode 1 send 5.0 coins to a valid taddr to verify the network split",self.nodes,DEBUG_MODE)

        txes.append(self.nodes[1].sendtoaddress("zthXuPst7DVeePf2ZQvodgyMfQCrYf9oVx4", 5.0))
        self.sync_all()

        # Check the mempools of every nodes
        mark_logs ("\nChecking mempools...",self.nodes,DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            txmem = self.nodes[i].getrawmempool()
            if i == 2:
                assert_equal(len(txmem), 0)
            else:
                assert_equal(sorted(txes), sorted(txmem))


        # ---------------------------------------------------------------------------------------
        # Nodes 1 creates the SC
        mark_logs("\nNode 1 creates the SC",self.nodes,DEBUG_MODE)

        amounts = [{"address": "dada", "amount": creation_amount}]
        tx_create = self.nodes[1].sc_create(scid, 123, amounts)
        txes.append(tx_create)
        self.sync_all()

        # Node 0 try create a SC with same id
        mark_logs("\nNode 0 try creates the same SC",self.nodes,DEBUG_MODE)
        try:
            self.nodes[0].sc_create(scid, 123, amounts)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs (errorString,self.nodes,DEBUG_MODE)
        assert_equal("Transaction commit failed" in errorString, True)

        mark_logs("\nNode0 generating 1 honest block",self.nodes,DEBUG_MODE)

        blocks.extend(self.nodes[0].generate(1))
        ownerBlock = blocks[-1]
        self.sync_all()

        # Node 1 creates a FT of 4.0 coins and Node 0 generates 1 block
        mark_logs("\nNode 1 performs a fwd transfer of " + str(fwt_amount_1) + " coins ...",self.nodes,DEBUG_MODE)
        txes.append(self.nodes[1].sc_send("abcd", fwt_amount_1, scid))

        mark_logs("\nNode0 generating 1 honest block",self.nodes,DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        # Node 1 creates a FT of 1.0 coin and Node 0 generates 1 block
        mark_logs("\nNode 1 performs a fwd transfer of " + str(fwt_amount_2) + " coins ...",self.nodes,DEBUG_MODE)
        txes.append(self.nodes[1].sc_send("abcd", fwt_amount_2, scid))
        self.sync_all()

        mark_logs("\nNode0 generating 1 honest block",self.nodes,DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        # Check the sc info
        mark_logs ("\nChecking sc info on 'honest' portion of network...",self.nodes,DEBUG_MODE)
        scinfoNode0 = self.nodes[0].getscinfo(scid)
        scinfoNode1 = self.nodes[1].getscinfo(scid)
        assert_equal(scinfoNode0, scinfoNode1)
        mark_logs ("Node 0: "+str(scinfoNode0),self.nodes,DEBUG_MODE)
        mark_logs ("Node 1: "+str(scinfoNode1),self.nodes,DEBUG_MODE)
        try:
            mark_logs ("Node 2: ", self.nodes[2].getscinfo(scid),self.nodes,DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs (errorString,self.nodes,DEBUG_MODE)

        assert_equal(self.nodes[1].getscinfo(scid)["balance"], creation_amount + fwt_amount_1 + fwt_amount_2)
        assert_equal(self.nodes[1].getscinfo(scid)["created in block"], ownerBlock)
        assert_equal(self.nodes[1].getscinfo(scid)["creating tx hash"], tx_create)
        assert_equal("scid not yet created" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Nodes 2 start to work on malicious chain
        mark_logs("\nNode 2 generates 4 malicious blocks, its chain will have a greater length than honest...",self.nodes,DEBUG_MODE)

        blocks.extend(self.nodes[2].generate(4))
        self.sync_all()

        mark_logs("\nJoining network",self.nodes,DEBUG_MODE)
        self.join_network()
        mark_logs("Network joined",self.nodes,DEBUG_MODE)

        mark_logs ("\nChecking that sc info on Node1 are not available anymore since tx has been reverted...",self.nodes,DEBUG_MODE)
        try:
            mark_logs (self.nodes[1].getscinfo(scid),self.nodes,DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs (errorString,self.nodes,DEBUG_MODE)
        assert_equal("scid not yet created" in errorString, True)

        # Check the mempools of every nodes
        mark_logs ("\nChecking mempools...",self.nodes,DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            txmem = self.nodes[i].getrawmempool()
            if i == 2:
                assert_equal(len(txmem), 0)
            else:
                assert_equal(sorted(txes), sorted(txmem))

        # Node 1 try to reuse inputs of FT
        mark_logs("\nNode 1 try to reuse inputs of FT...",self.nodes,DEBUG_MODE)

        assert(self.nodes[1].getbalance()<1)
        # ---------------------------------------------------------------------------------------
        # Node 1 restores honest chain
        mark_logs("\nNode1 generating 1 honest block and restoring the SC creation...",self.nodes,DEBUG_MODE)

        blocks.extend(self.nodes[1].generate(1))
        secondOwnerBlock = blocks[-1]

        mark_logs("\nNode1 generating 1 honest block more and restoring all of SC funds...",self.nodes,DEBUG_MODE)
        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        # Check the mempools of every nodes
        mark_logs ("\nChecking mempools...",self.nodes,DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            txmem = self.nodes[i].getrawmempool()
            assert_equal(len(txmem), 0)

        mark_logs ("\nChecking sc info on the whole network...",self.nodes,DEBUG_MODE)
        scinfoNode0 = self.nodes[0].getscinfo(scid)
        scinfoNode1 = self.nodes[1].getscinfo(scid)
        scinfoNode2 = self.nodes[2].getscinfo(scid)

        mark_logs ("Node 0: "+ str(scinfoNode0),self.nodes,DEBUG_MODE)
        mark_logs ("Node 1: "+ str(scinfoNode1),self.nodes,DEBUG_MODE)
        mark_logs ("Node 2: "+ str(scinfoNode2),self.nodes,DEBUG_MODE)

        assert_equal(scinfoNode0, scinfoNode1)
        assert_equal(scinfoNode0, scinfoNode2)
        assert_equal(self.nodes[2].getscinfo(scid)["balance"], creation_amount + fwt_amount_1 + fwt_amount_2)
        assert_equal(self.nodes[2].getscinfo(scid)["created in block"], secondOwnerBlock)
        assert_equal(self.nodes[1].getscinfo(scid)["creating tx hash"], tx_create)


if __name__ == '__main__':
    ScSplitTest().main()
