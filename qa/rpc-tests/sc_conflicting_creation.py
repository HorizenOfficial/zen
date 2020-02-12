#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, p2p_port, \
    mark_logs, disconnect_nodes
import os
from decimal import Decimal
import time

NUMB_OF_NODES = 2
DEBUG_MODE = 0
SC_COINS_MAT = 2


class ScConflictingCreation(BitcoinTestFramework):
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
                                 extra_args=[["-sccoinsmaturity=%d" % SC_COINS_MAT, '-logtimemicros=1', '-debug=sc',
                                              '-debug=py', '-debug=mempool', '-debug=net',
                                              '-debug=bench']] * NUMB_OF_NODES)

        if not split:
            # 0 and 1 are joint only if split==false
            connect_nodes_bi(self.nodes, 0, 1)
            sync_blocks(self.nodes[1:NUMB_OF_NODES])
            sync_mempools(self.nodes[1:NUMB_OF_NODES])

        self.is_network_split = split
        self.sync_all()

    def split_network(self):
        # Split the network of two nodes into nodes 0 and 1.
        assert not self.is_network_split
        disconnect_nodes(self.nodes[0], 1)
        disconnect_nodes(self.nodes[1], 0)
        self.is_network_split = True

    def join_network(self):
        # Join the (previously split) network pieces together: 0-1
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 0)
        time.sleep(2)
        self.is_network_split = False

    def run_test(self):
        '''
        This test shows how a SC creation tx race through the network is handled.
        While different txs declaring the same SC can exist in two nodes' mempools,
        the mining process solve the duplication confirming one the SC creation tx
        and removing the conflicting one from mempools.
        Fwd txs are not affected by the duplication cleanup and are duly confirmed.
        '''
        # network topology: (0)-(1)
        scid = "22"

        creation_amount_0 = Decimal("10.0")
        fwt_amount_0 = Decimal("20.0")
        amounts_0 = [{"address": "dada", "amount": creation_amount_0}]

        creation_amount_1 = Decimal("1.0")
        fwt_amount_1 = Decimal("2.0")
        amounts_1 = [{"address": "baba", "amount": creation_amount_1}]

        mark_logs("Node 0 generates 110 block", self.nodes,DEBUG_MODE)
        self.nodes[0].generate(110)
        self.sync_all()

        mark_logs("Node 1 generates 110 block", self.nodes,DEBUG_MODE)
        self.nodes[1].generate(110)
        self.sync_all()

        # Split the network: (0) / (1)
        mark_logs("\nSplit network", self.nodes,DEBUG_MODE)
        self.split_network()
        mark_logs("The network is split: 0 / 1", self.nodes,DEBUG_MODE)

        mark_logs("\nNode 0 create a SC...", self.nodes, DEBUG_MODE)
        try:
            sc_by_0 = self.nodes[0].sc_create(scid, 123, amounts_0)
            if DEBUG_MODE != 0: print "sc_by_0   = ", sc_by_0
        except JSONRPCException, e:
            errorString = e.error['message']
            if DEBUG_MODE != 0: print (errorString)

        mark_logs("\nNode 0 send funds to the SC...", self.nodes, DEBUG_MODE)
        try:
            fwt_by_0 = self.nodes[0].sc_send("abcd", fwt_amount_0, scid)
            if DEBUG_MODE != 0: print "fwt_by_0 = ", fwt_by_0
        except JSONRPCException, e:
            errorString = e.error['message']
            if DEBUG_MODE != 0: print (errorString)        

        mark_logs("\nNode 1 create a SC using the same scid...", self.nodes, DEBUG_MODE)
        try:
            sc_by_1 = self.nodes[1].sc_create(scid, 123, amounts_1)
            if DEBUG_MODE != 0: print "sc_by_1   = ", sc_by_1
        except JSONRPCException, e:
            errorString = e.error['message']
            if DEBUG_MODE != 0: print (errorString)

        mark_logs("\nNode 0 send funds to the SC...", self.nodes, DEBUG_MODE)
        try:
            fwt_by_1 = self.nodes[1].sc_send("abcd", fwt_amount_1, scid)
            if DEBUG_MODE != 0: print "fwt_by_1 = ", fwt_by_1
        except JSONRPCException, e:
            errorString = e.error['message']
            if DEBUG_MODE != 0: print (errorString)

        mark_logs("\nJoining network", self.nodes,DEBUG_MODE)
        self.join_network()
        mark_logs("Network joined", self.nodes,DEBUG_MODE)

        # Mempools tell different candidate histories
        mark_logs("Check that txes pair are in the proper mempool", self.nodes,DEBUG_MODE)
        assert_equal(sc_by_0 in self.nodes[0].getrawmempool(), True)
        assert_equal(fwt_by_0 in self.nodes[0].getrawmempool(), True)
        assert_equal(sc_by_1 in self.nodes[0].getrawmempool(), False)
        assert_equal(fwt_by_1 in self.nodes[0].getrawmempool(), False)
        
        assert_equal(sc_by_1 in self.nodes[1].getrawmempool(), True)
        assert_equal(fwt_by_1 in self.nodes[1].getrawmempool(), True)
        assert_equal(sc_by_0 in self.nodes[1].getrawmempool(), False)
        assert_equal(fwt_by_0 in self.nodes[1].getrawmempool(), False)
        if DEBUG_MODE != 0: print ("...OK")

        mark_logs("Node 1 generates 1 block", self.nodes,DEBUG_MODE)
        self.nodes[1].generate(1)
        sync_blocks(self.nodes)

        # Once a block is mined, creation from node1 is accepted by node0
        mark_logs("Check that creation tx from Node 1 is accepted into Node0's chain best block", self.nodes,DEBUG_MODE)
        assert_equal(sc_by_1 in self.nodes[0].getblock(self.nodes[0].getbestblockhash(), True)['tx'], True )
        if DEBUG_MODE != 0: print ("...OK")

        # Both nodes see the Sc created by creation tx coming from node 1
        mark_logs("Check that both Nodes see SC created by tx from Node1", self.nodes,DEBUG_MODE)
        assert_equal(sc_by_1, self.nodes[0].getscinfo(scid)['creating tx hash'])
        assert_equal(sc_by_1, self.nodes[1].getscinfo(scid)['creating tx hash'])
        if DEBUG_MODE != 0: print ("...OK")

        # both SC creation txs are removed from the mempools
        mark_logs("Check that both creation txes have been removed from the mempools", self.nodes,DEBUG_MODE)
        assert_equal(sc_by_0 in self.nodes[0].getrawmempool(), False)        
        assert_equal(sc_by_0 in self.nodes[1].getrawmempool(), False)
        assert_equal(sc_by_1 in self.nodes[0].getrawmempool(), False)
        assert_equal(sc_by_1 in self.nodes[1].getrawmempool(), False)
        if DEBUG_MODE != 0: print ("...OK")

        # while fwd txs are still in their mempools
        mark_logs("Check that each fwd txs are in their node mempools", self.nodes,DEBUG_MODE)
        assert_equal(fwt_by_0 in self.nodes[0].getrawmempool(), True)
        assert_equal(fwt_by_1 in self.nodes[0].getrawmempool(), False)
        assert_equal(fwt_by_0 in self.nodes[1].getrawmempool(), False)
        assert_equal(fwt_by_1 in self.nodes[1].getrawmempool(), True)
        if DEBUG_MODE != 0: print ("...OK")

        # next block mined by Node1 confirms fwt_by_1
        mark_logs("Node 1 generates 1 block", self.nodes,DEBUG_MODE)
        self.nodes[1].generate(1)
        sync_blocks(self.nodes)

        # Once a block is mined, fwd tx from node1 is accepted by node0
        mark_logs("Check that fwd tx from Node 1 is not in Node0's chain best block", self.nodes,DEBUG_MODE)
        assert_equal(fwt_by_1 in self.nodes[0].getblock(self.nodes[0].getbestblockhash(), True)['tx'], True ) 
        if DEBUG_MODE != 0: print ("...OK")

        # fwd tx of node0 is still in mempool
        mark_logs("Check that fwd tx from Node0 is still in its node mempool", self.nodes,DEBUG_MODE)
        assert_equal(fwt_by_0 in self.nodes[0].getrawmempool(), True)
        if DEBUG_MODE != 0: print ("...OK")

        # ...while node1 mempool is empty now
        mark_logs("Check that now Node1 has an empty mempool", self.nodes,DEBUG_MODE)
        assert_equal(len(self.nodes[1].getrawmempool()), 0)
        if DEBUG_MODE != 0: print ("...OK")

        # next block mined by Node0 confirms fwt_by_0
        mark_logs("Node 0 generates 1 block", self.nodes,DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # Once a block is mined, fwd tx from node0 is accepted by node1
        mark_logs("Check that fwd tx from Node 0 is in Node1's chain best block", self.nodes,DEBUG_MODE)
        assert_equal(fwt_by_0 in self.nodes[1].getblock(self.nodes[1].getbestblockhash(), True)['tx'], True )
        if DEBUG_MODE != 0: print ("...OK")

        # node0 mempool is also empty now
        mark_logs("Check that now also Node0 has an empty mempool", self.nodes,DEBUG_MODE)
        assert_equal(len(self.nodes[0].getrawmempool()), 0)
        if DEBUG_MODE != 0: print ("...OK")

        # both nodes have the same view of the sc 
        mark_logs("Check that both Nodes sees the same sc info", self.nodes,DEBUG_MODE)
        assert_equal(self.nodes[0].getscinfo(scid), self.nodes[0].getscinfo(scid))
        if DEBUG_MODE != 0: print ("...OK")

        mark_logs(("Node 0 generates %d block" % SC_COINS_MAT), self.nodes,DEBUG_MODE)
        self.nodes[0].generate(SC_COINS_MAT)
        self.sync_all()

        # SC balance has creation amount of good creation and all of the fw txes 
        mark_logs("Check that sc amount include creation amount from Node1 and both fw txes", self.nodes,DEBUG_MODE)
        assert_equal(self.nodes[0].getscinfo(scid), self.nodes[0].getscinfo(scid))
        assert_equal(self.nodes[0].getscinfo(scid)['balance'], creation_amount_1 + fwt_amount_0 + fwt_amount_1)
        if DEBUG_MODE != 0: print ("...OK")


if __name__ == '__main__':
    ScConflictingCreation().main()
