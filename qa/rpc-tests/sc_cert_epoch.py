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
import pprint

import time

EPOCH_LENGTH = 5
NUMB_OF_NODES = 4

class sc_cert_epoch(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-logtimemicros=1', '-zapwallettxes=2']] * NUMB_OF_NODES )

        idx=0
        for nod in self.nodes:
            if idx < (NUMB_OF_NODES-1):
                connect_nodes_bi(self.nodes, idx, idx+1)
                idx += 1

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def disconnect_nodes(self, from_connection, node_num):
        ip_port = "127.0.0.1:"+str(p2p_port(node_num))
        from_connection.disconnectnode(ip_port)
        # poll until version handshake complete to avoid race conditions
        # with transaction relaying
        while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
            time.sleep(0.1)

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
        for nod in self.nodes:
            nod.dbg_log(msg)

    def run_test(self):

        # side chain id
        scid = "1111111111111111111111111111111111111111111111111111111111111111"

        #forward transfer amount
        creation_amount = Decimal("0.5")
        fwt_amount = Decimal("50")
        bwt_amount = Decimal("25")

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

        # node 1 has just the coinbase which is now maturee
        bal_before = self.nodes[1].getbalance("", 0)
        print "\nNode1 balance: ", bal_before

        self.mark_logs("\nNode 1 creates the SC spending " + str(creation_amount) + " coins ...")
        amounts = []
        amounts.append( {"address":"dada", "amount": creation_amount})
        creating_tx = self.nodes[1].sc_create(scid, EPOCH_LENGTH, amounts);
        print "creating_tx = " + creating_tx
        self.sync_all()

        self.mark_logs("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        ownerBlock = blocks[-1]
        self.sync_all()

        self.mark_logs("\nNode 0 performs a fwd transfer of " + str(fwt_amount) + " coins to SC...")

        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount, scid);
        print "fwd_tx=" + fwd_tx
        self.sync_all()

        self.mark_logs("\nNode0 generating 5 block")
        blocks.extend(self.nodes[0].generate(5))
        self.sync_all()

        print "\nSC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))

        epn = 1
        eph = blocks[-1]

        pkh_node2 = self.nodes[2].getnewaddress("", True);
        amounts = []
        amounts.append( {"pubkeyhash":pkh_node2, "amount": bwt_amount})
        cert = []

        self.mark_logs("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        print "Node1 balance: ", self.nodes[1].getbalance()
        print "Node2 balance: ", self.nodes[2].getbalance()
        print "Node3 balance: ", self.nodes[3].getbalance()

        try:
            cert = self.nodes[0].send_certificate(scid, epn, eph, amounts);
            print "cert=", cert
        except JSONRPCException,e:
            errorString = e.error['message']
            print "\n======> ", errorString

        self.sync_all()

        self.mark_logs("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        print "\nSC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))

        print "Node1 balance: ", self.nodes[1].getbalance()
        print "Node2 balance: ", self.nodes[2].getbalance()
        print "Node3 balance: ", self.nodes[3].getbalance()

        # now Node2 use the UTXO related to certificate for sending coins to Node3
        self.mark_logs("\nNode 2 sends " + str(bwt_amount/2) + " coins to Node3...")
        tx = self.nodes[2].sendtoaddress(self.nodes[3].getnewaddress(), bwt_amount/2)
        #tx_self = self.nodes[2].sendtoaddress(self.nodes[2].getnewaddress(), bwt_amount/4)
        print "this tx uses certificate as input:"
        print "tx = ", tx
        #print "tx_self = ", tx_self
        print
        self.sync_all()

        self.mark_logs("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        print "Node1 balance: ", self.nodes[1].getbalance()
        print "Node2 balance: ", self.nodes[2].getbalance()
        print "      z_balance: ", self.nodes[2].z_gettotalbalance()['transparent']
        print "Node3 balance: ", self.nodes[3].getbalance()
        print

        #-------------------------------------------------------------
        for j in range(0, 4):
            #raw_input("press to invalidate...")

            invalidating = self.nodes[0].getbestblockhash()
            self.mark_logs("\nNode 0 invalidates last block...")
            print "Invalidating: ", invalidating
            self.nodes[0].invalidateblock(invalidating)
            time.sleep(1)

            try:
                print "Node0 mempool: ", self.nodes[0].getrawmempool()
                print "Node0 SC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))
                print
            except JSONRPCException,e:
                errorString = e.error['message']
                print errorString
       
        self.mark_logs("\nNode0 generating 4 block")
        blocks.extend(self.nodes[0].generate(4))
        self.sync_all()

        print 
        for i in range(0, NUMB_OF_NODES):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print "---"
            print "\nSC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))

        print "Node1 balance: ", self.nodes[1].getbalance()
        print "Node2 balance: ", self.nodes[2].getbalance()
        print "      z_balance: ", self.nodes[2].z_gettotalbalance()['transparent']
        print "Node3 balance: ", self.nodes[3].getbalance()
        print

        self.mark_logs("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        print 
        for i in range(0, NUMB_OF_NODES):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print "---"
            print "\nSC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))

        print "Node1 balance: ", self.nodes[1].getbalance()
        print "Node2 balance: ", self.nodes[2].getbalance()
        print "      z_balance: ", self.nodes[2].z_gettotalbalance()['transparent']
        print "Node3 balance: ", self.nodes[3].getbalance()
        print

        self.mark_logs("\nNode 2 sends " + str(bwt_amount/2) + " coins to Node3...")
        tx = []
        try:
            tx = self.nodes[2].sendtoaddress(self.nodes[3].getnewaddress(), bwt_amount/2)
        except JSONRPCException,e:
            errorString = e.error['message']
            print errorString
       
        self.mark_logs("\nStopping nodes")
        stop_nodes(self.nodes)
        wait_bitcoinds()
        print "\nRestarting nodes"
        self.setup_network(False)
        self.mark_logs("\nRestarted nodes")

        print "Node1 balance: ", self.nodes[1].getbalance()
        print "Node2 balance: ", self.nodes[2].getbalance()
        print "      z_balance: ", self.nodes[2].z_gettotalbalance()['transparent']
        print "Node3 balance: ", self.nodes[3].getbalance()
        print



if __name__ == '__main__':
    sc_cert_epoch().main()
