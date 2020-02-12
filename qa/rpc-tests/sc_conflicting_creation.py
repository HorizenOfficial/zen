#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, p2p_port, \
    mark_logs
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

    def disconnect_nodes(self, from_connection, node_num):
        ip_port = "127.0.0.1:" + str(p2p_port(node_num))
        from_connection.disconnectnode(ip_port)
        # poll until version handshake complete to avoid race conditions
        # with transaction relaying
        while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
            time.sleep(0.1)

    def split_network(self):
        # Split the network of two nodes into nodes 0 and 1.
        assert not self.is_network_split
        self.disconnect_nodes(self.nodes[0], 1)
        self.disconnect_nodes(self.nodes[1], 0)
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
        This test try to create the same SC and forward funds in two nodes which are disjointed
        The network is then joined and mining takes place. At the end, one of the creation tx is removed from mempool and 
        all the funds are gathered in the persisted SC
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

        tx0 = []
        tx0_f = []

        tx1 = []
        tx1_f = []

        # Split the network: (0)--(1) / (2)
        mark_logs("\nSplit network", self.nodes,DEBUG_MODE)
        self.split_network()
        mark_logs("The network is split: 0 .. 1", self.nodes,DEBUG_MODE)

        mark_logs("\nNode 0 create a SC...", self.nodes, DEBUG_MODE)
        try:
            tx0 = self.nodes[0].sc_create(scid, 123, amounts_0)
            if DEBUG_MODE != 0: print "tx0   = ", tx0
        except JSONRPCException, e:
            errorString = e.error['message']
            if DEBUG_MODE != 0: print (errorString)

        mark_logs("\nNode 0 send funds to the SC...", self.nodes, DEBUG_MODE)
        try:
            tx0_f = self.nodes[0].sc_send("abcd", fwt_amount_0, scid)
            if DEBUG_MODE != 0: print "tx0_f = ", tx0_f
        except JSONRPCException, e:
            errorString = e.error['message']
            if DEBUG_MODE != 0: print (errorString)

        mark_logs("\nNode 1 create a SC using the same scid...", self.nodes, DEBUG_MODE)
        try:
            tx1 = self.nodes[1].sc_create(scid, 123, amounts_1)
            if DEBUG_MODE != 0: print "tx1   = ", tx1
        except JSONRPCException, e:
            errorString = e.error['message']
            if DEBUG_MODE != 0: print (errorString)

        mark_logs("\nNode 0 send funds to the SC...", self.nodes, DEBUG_MODE)
        try:
            tx1_f = self.nodes[1].sc_send("abcd", fwt_amount_1, scid)
            if DEBUG_MODE != 0: print "tx1_f = ", tx1_f
        except JSONRPCException, e:
            errorString = e.error['message']
            if DEBUG_MODE != 0: print (errorString)

        sync_blocks(self.nodes[1:2])
        sync_mempools(self.nodes[1:2])
        sync_blocks(self.nodes[0:1])
        sync_mempools(self.nodes[0:1])

        mark_logs("\nJoining network", self.nodes,DEBUG_MODE)
        self.join_network()
        mark_logs("Network joined", self.nodes,DEBUG_MODE)

        # txes pair are in the proper mempool
        mark_logs("Check that txes pair are in the proper mempool", self.nodes,DEBUG_MODE)
        assert_equal(tx0 in self.nodes[0].getrawmempool(), True)
        assert_equal(tx0_f in self.nodes[0].getrawmempool(), True)
        assert_equal(tx1 in self.nodes[1].getrawmempool(), True)
        assert_equal(tx1_f in self.nodes[1].getrawmempool(), True)
        if DEBUG_MODE != 0: print ("...OK")

        mark_logs("Node 1 generates 1 block", self.nodes,DEBUG_MODE)
        self.nodes[1].generate(1)
        sync_blocks(self.nodes)

        # fw txes are in the proper mempool
        mark_logs("Check that each fw tx is in its node mempool", self.nodes,DEBUG_MODE)
        assert_equal(tx0_f in self.nodes[0].getrawmempool(), True)
        assert_equal(tx1_f in self.nodes[1].getrawmempool(), True)
        if DEBUG_MODE != 0: print ("...OK")
        # creations are not
        mark_logs("Check that both creation txes have been removed from node mempool", self.nodes,DEBUG_MODE)
        assert_equal(tx0 in self.nodes[0].getrawmempool(), False)
        assert_equal(tx1 in self.nodes[1].getrawmempool(), False)
        if DEBUG_MODE != 0: print ("...OK")

        # but creation from node1 is in the best block (check node0)
        mark_logs("Check that creation tx from Node 1 is in the chain best block", self.nodes,DEBUG_MODE)
        assert_equal(tx1 in self.nodes[0].getblock(self.nodes[0].getbestblockhash(), True)['tx'], True )
        if DEBUG_MODE != 0: print ("...OK")
        # while creation from node0 is not in the best block either
        mark_logs("Check that creation tx from Node 0 is NOT in the chain best block", self.nodes,DEBUG_MODE)
        assert_equal(tx0 in self.nodes[0].getblock(self.nodes[0].getbestblockhash(), True)['tx'], False )
        if DEBUG_MODE != 0: print ("...OK")

        mark_logs("Node 1 generates 1 block", self.nodes,DEBUG_MODE)
        self.nodes[1].generate(1)
        sync_blocks(self.nodes)

        # fw tx of node0 is still in mempool 
        mark_logs("Check that fw tx from Node0 is still in its node mempool", self.nodes,DEBUG_MODE)
        assert_equal(tx0_f in self.nodes[0].getrawmempool(), True)
        if DEBUG_MODE != 0: print ("...OK")
        # node1 mempool is empty now
        mark_logs("Check that now Node1 has an empty mempool", self.nodes,DEBUG_MODE)
        assert_equal(len(self.nodes[1].getrawmempool()), 0)
        if DEBUG_MODE != 0: print ("...OK")

        mark_logs("Node 0 generates 1 block", self.nodes,DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # node0 mempool is also empty now
        mark_logs("Check that now also Node0 has an empty mempool", self.nodes,DEBUG_MODE)
        assert_equal(len(self.nodes[0].getrawmempool()), 0)
        if DEBUG_MODE != 0: print ("...OK")
        # and block has it (check node 1)
        mark_logs("Check that fw tx from Node 0 is in the chain best block", self.nodes,DEBUG_MODE)
        assert_equal(tx0_f in self.nodes[1].getblock(self.nodes[1].getbestblockhash(), True)['tx'], True )
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
