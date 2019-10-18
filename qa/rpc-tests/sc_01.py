#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, start_node, connect_nodes, stop_node, stop_nodes, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, p2p_port, check_json_precision
import traceback
import os,sys
import shutil
from random import randint
from decimal import Decimal
import logging

import time
class headers(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(3, self.options.tmpdir, extra_args=[['-debug=sc']] * 3 )

        if not split:
            # 1 and 2 are joint only if split==false
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:3])
            sync_mempools(self.nodes[1:3])

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def disconnect_nodes(self, from_connection, node_num):
        ip_port = "127.0.0.1:"+str(p2p_port(node_num))
        from_connection.disconnectnode(ip_port)
        # poll until version handshake complete to avoid race conditions
        # with transaction relaying
        while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
            time.sleep(0.1)

    def split_network(self):
        # Split the network of three nodes into nodes 0-1 and 2.
        assert not self.is_network_split
        self.disconnect_nodes(self.nodes[1], 2)
        self.disconnect_nodes(self.nodes[2], 1)
        self.is_network_split = True

    def join_network(self):
        #Join the (previously split) network pieces together: 0-1-2
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 1)
        #self.sync_all()
        time.sleep(2)
        self.is_network_split = False

    def dump_ordered_tips(self, tip_list):
        sorted_x = sorted(tip_list, key=lambda k: k['status'])
        c = 0
        for y in sorted_x:
            if (c == 0):
                print y 
            else:
                print " ",y 
            c = 1

    def mark_logs(self, msg):
        print msg
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)
        self.nodes[2].dbg_log(msg)

    def run_test(self):

        # side chain id
        scid = "1111111111111111111111111111111111111111111111111111111111111111"

        #forward transfer amount
        creation_amount = Decimal("0.5")
        fwt_amount = Decimal("2.5")

        blocks = []
        self.bl_count = 0

        blocks.append(self.nodes[0].getblockhash(0))

        # node 1 earns some coins, they would be available after 100 blocks 
        self.mark_logs("Node 1 generates 1 block")

        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        self.mark_logs("Node 0 generates 220 block")

        blocks.extend(self.nodes[0].generate(220))
        self.sync_all()

        # One portion is (0,1) the other is (2)
        self.mark_logs("\nSplit network")
        self.split_network()
        self.mark_logs("The network is split: 0-1 .. 2")

        print "\nNode1 balance: ", self.nodes[1].getbalance("", 0)

        self.mark_logs("\nNode 1 creates the SC")
        amounts = []
        amounts.append( {"address":"dada", "amount": creation_amount})
        creating_tx = self.nodes[1].sc_create(scid, 123, amounts);
        print "tx=" + creating_tx
        self.sync_all()

        self.mark_logs("\nVerify that once mempool is synced, no one can create the same SC..")
        try:
            self.mark_logs("\nNode 0 tries to create a SC with the same id")
            self.nodes[0].sc_create(scid, 123456, amounts);
        except JSONRPCException,e:
            errorString = e.error['message']
            print errorString

        assert_equal("Transaction commit failed" in errorString, True);

        self.mark_logs("\nNode 1 send 5.0 coins to a valid taddr")
        tx = self.nodes[1].sendtoaddress("zthXuPst7DVeePf2ZQvodgyMfQCrYf9oVx4", 5.0);
        print "tx=" + tx
        self.sync_all()

        print "\nChecking mempools..."
        print "Node 0: ", self.nodes[0].getrawmempool()
        print "Node 1: ", self.nodes[1].getrawmempool()
        print "Node 2: ", self.nodes[2].getrawmempool()

        self.mark_logs("\nNode0 generating 1 honest block")
        blocks.extend(self.nodes[0].generate(1))
        ownerBlock = blocks[-1]
        print ownerBlock
        self.sync_all()

        print "\nChecking sc info on 'honest' portion of network..."
        print "Node 0: ", self.nodes[0].getscinfo(scid)
        print "Node 1: ", self.nodes[1].getscinfo(scid)

        self.mark_logs("\nNode 1 performs a fwd transfer of "+str(fwt_amount)+" coins ...")

        tx = self.nodes[1].sc_send("abcd", fwt_amount, scid);
        print "tx=" + tx
        self.sync_all()

        print("\nNode0 generating 1 honest block")
        blocks.extend(self.nodes[0].generate(1))
        print blocks[-1]
        self.sync_all()

        print "\nNode1 balance: ", self.nodes[1].getbalance("", 0)

        print "\nChecking sc info on 'honest' portion of network..."
        print self.nodes[1].getscinfo(scid)
        print self.nodes[0].getscinfo(scid)

        assert_equal(self.nodes[1].getscinfo(scid)["balance"], creation_amount + fwt_amount) 
        assert_equal(self.nodes[1].getscinfo(scid)["created in block"], ownerBlock) 
        assert_equal(self.nodes[1].getscinfo(scid)["creating tx hash"], creating_tx) 

        self.mark_logs("\nNode 2 generates 3 malicious blocks, its chain will have a greater length than honest...")
        blocks.extend(self.nodes[2].generate(3))
        print blocks[-1]
        self.sync_all()

        self.mark_logs("\nJoining network")
        self.join_network()
        time.sleep(2)
        self.mark_logs("Network joined")

        print 
        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print "---"

        print "\nChecking that sc info on Node1 are not available anymore since tx has been reverted..."
        try:
            print self.nodes[1].getscinfo(scid)
        except JSONRPCException,e:
            errorString = e.error['message']
            print errorString

        assert_equal("scid not yet created" in errorString, True);

        print "\nChecking mempools..."
        print "Node 0: ", self.nodes[0].getrawmempool()
        print "Node 1: ", self.nodes[1].getrawmempool()
        print "Node 2: ", self.nodes[2].getrawmempool()

        print "\nNode1 balance: ", self.nodes[1].getbalance("", 0)

        self.mark_logs("\nNode1 generating 1 honest block and restoring the SC creation...")
        blocks.extend(self.nodes[1].generate(1))
        secondOwnerBlock = blocks[-1]
        print secondOwnerBlock
        time.sleep(2)
        #self.sync_all()

        print "\nChecking mempools..."
        print "Node 0: ", self.nodes[0].getrawmempool()
        print "Node 1: ", self.nodes[1].getrawmempool()
        print "Node 2: ", self.nodes[2].getrawmempool()

        self.mark_logs("\nNode1 generating 1 honest block more and restoring all of SC funds...")
        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        print "\nChecking mempools..."
        print "Node 0: ", self.nodes[0].getrawmempool()
        print "Node 1: ", self.nodes[1].getrawmempool()
        print "Node 2: ", self.nodes[2].getrawmempool()

        print "\nChecking sc info on the whole network..."
        print "Node 0: ", self.nodes[0].getscinfo(scid)
        print "Node 1: ", self.nodes[1].getscinfo(scid)
        print "Node 2: ", self.nodes[2].getscinfo(scid)

        assert_equal(self.nodes[2].getscinfo(scid)["balance"], creation_amount + fwt_amount) 
        assert_equal(self.nodes[2].getscinfo(scid)["created in block"], secondOwnerBlock) 
        assert_equal(self.nodes[1].getscinfo(scid)["creating tx hash"], creating_tx) 

        self.mark_logs("\nVerify that no one can create the same SC in the blockchain..")
        try:
            self.mark_logs("\nNode 2 tries to create the same SC")
            self.nodes[2].sc_create(scid, 456, amounts);
        except JSONRPCException,e:
            errorString = e.error['message']
            print errorString

        assert_equal("already created" in errorString, True);
        print


if __name__ == '__main__':
    headers().main()
