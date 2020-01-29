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

class sc_cert_base(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(3, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-logtimemicros=1', '-disablesafemode=1']] * 3 )

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[1:3])
        sync_mempools(self.nodes[1:3])
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
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)
        self.nodes[2].dbg_log(msg)

    def run_test(self):

        # side chain id
        scid = "1111111111111111111111111111111111111111111111111111111111111111"

        #forward transfer amount
        creation_amount = Decimal("0.5")
        fwt_amount_1    = Decimal("1000.0")
        bwt_amount_1    = Decimal("8.0")
        fwt_amount_2    = Decimal("1.0")
        fwt_amount_3    = Decimal("2.0")
        fwt_amount_4    = Decimal("3.0")
        bwt_amount_2    = Decimal("8.5")

        blocks = []
        sc_info = []
        balance_node0 = []
        self.bl_count = 0

        blocks.append(self.nodes[0].getblockhash(0))

        # node 1 earns some coins, they would be available after 100 blocks 
        self.mark_logs("Node 1 generates 1 block")

        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        self.mark_logs("Node 0 generates 220 block")

        blocks.extend(self.nodes[0].generate(220))
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append("No SC")

        self.mark_logs("\nNode 1 creates the SC spending " + str(creation_amount) + " coins ...")
        amounts = []
        amounts.append( {"address":"dada", "amount": creation_amount})
        creating_tx = self.nodes[0].sc_create(scid, EPOCH_LENGTH, amounts);
        print "creating_tx = " + creating_tx
        self.sync_all()

        self.mark_logs("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        sc_creating_block = blocks[-1]
        sc_creating_height = self.nodes[0].getblockcount()
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(sc_info[-1])

        self.mark_logs("\nNode 0 performs a fwd transfer of " + str(fwt_amount_1) + " coins to SC...")
        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount_1, scid);
        print "fwd_tx=" + fwd_tx
        self.sync_all()

        print("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(sc_info[-1])

        pkh_node1 = self.nodes[1].getnewaddress("", True);
        pkh_node2 = self.nodes[2].getnewaddress("", True);
        amounts = []

        self.mark_logs("\nNode0 generating 3 more blocks for achieving sc coins maturity")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(sc_info[-1])

        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(sc_info[-1])

        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(sc_info[-1])

        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(sc_info[-1])

        current_height = self.nodes[0].getblockcount()

        epn = int((current_height - sc_creating_height) / EPOCH_LENGTH)
        print " h=", current_height
        print "ch=", sc_creating_height
        print "epn=", epn
        eph = self.nodes[0].getblockhash(sc_creating_height + (epn*EPOCH_LENGTH))
        print "epn = ", epn, ", eph = ", eph

        
        self.mark_logs("\nNode 0 performs a bwd transfer of " + str(bwt_amount_1) + " coins to Node1 pkh[" + str(pkh_node1)+"]...")
        amounts.append( {"pubkeyhash":pkh_node1, "amount": bwt_amount_1})
        cert = self.nodes[0].send_certificate(scid, epn, eph, amounts);
        print "cert = " + cert
        self.sync_all()

        print("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(sc_info[-1])

        self.mark_logs("\nNode 0 performs a fwd transfer of " + str(fwt_amount_2) + " coins to SC...")
        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount_2, scid);
        print "fwd_tx=" + fwd_tx
        self.sync_all()

        print("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(sc_info[-1])

        self.mark_logs("\nNode 0 performs a fwd transfer of " + str(fwt_amount_3) + " coins to SC...")
        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount_3, scid);
        print "fwd_tx=" + fwd_tx
        self.sync_all()

        print("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(sc_info[-1])

        self.mark_logs("\nNode 0 performs a fwd transfer of " + str(fwt_amount_4) + " coins to SC...")
        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount_4, scid);
        print "fwd_tx=" + fwd_tx
        self.sync_all()

        print("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(sc_info[-1])

        self.mark_logs("\nNode0 generating 3 more blocks for achieving sc coins maturity")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(sc_info[-1])

        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(sc_info[-1])

        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(sc_info[-1])

        amounts = []

        current_height = self.nodes[0].getblockcount()

        epn = int((current_height - sc_creating_height) / EPOCH_LENGTH)
        print " h=", current_height
        print "ch=", sc_creating_height
        print "epn=", epn
        eph = self.nodes[0].getblockhash(sc_creating_height + (epn*EPOCH_LENGTH))
        print "epn = ", epn, ", eph = ", eph

        self.mark_logs("\nNode 0 performs a bwd transfer of " + str(bwt_amount_2) + " coins to Node1 pkh[" + str(pkh_node2)+"]...")
        amounts.append( {"pubkeyhash":pkh_node2, "amount": bwt_amount_2})
        cert = self.nodes[0].send_certificate(scid, epn, eph, amounts);
        print "cert = " + cert
        self.sync_all()

        print("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()
        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        balance_node0.append(self.nodes[0].getbalance("", 0))

        sc_info.append(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(sc_info[-1])

        #print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        print "Node1 balance: ", self.nodes[1].getbalance("", 0)
        print "Node2 balance: ", self.nodes[2].getbalance("", 0)
#        print "Node3 balance: ", self.nodes[3].getbalance("", 0)
        print "=================================================="
        print 

        print "sc_info len       = ", len(sc_info)
        print "balance_node0 len = ", len(balance_node0)

        # TODO loop on the whole range (currently fails on last cycle)
        #-------------------------------------------------------------
        for j in range(0, len(sc_info)):
            #if j > 12:
            #    raw_input("press to invalidate...")
            invalidating = self.nodes[0].getbestblockhash()
            self.mark_logs("\nNode 0 invalidates last block...")
            print "Invalidating: ", invalidating
            self.nodes[0].invalidateblock(invalidating)
            time.sleep(1)
            print "sc_info len       = ", len(sc_info)
            print "balance_node0 len = ", len(balance_node0)
            sc_info.pop()
            balance_node0.pop()
            try:
                assert_equal( self.nodes[0].getscinfo(scid), sc_info[-1])
                print "\nSC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))
            except JSONRPCException,e:
                errorString = e.error['message']
                print errorString
                print pprint.pprint(sc_info[-1])

            try:
                print "Node0 balance: ", self.nodes[0].getbalance("", 0)
                print "          was: ", balance_node0[-1]
                print " diff: ", (self.nodes[0].getbalance("", 0) - balance_node0[-1])
            except JSONRPCException,e:
                errorString = e.error['message']
                print errorString
       
        try:
            print "\nSC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))
        except JSONRPCException,e:
            errorString = e.error['message']
            print errorString

        try:
            print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        except JSONRPCException,e:
            errorString = e.error['message']
            print errorString

        print "Node1 balance: ", self.nodes[1].getbalance("", 0)
        print "Node2 balance: ", self.nodes[2].getbalance("", 0)
#        print "Node3 balance: ", self.nodes[3].getbalance("", 0)
        print "=================================================="
        print 

        self.mark_logs("\nNode0 generating 150 more blocks for achieving sc coins maturity")
        blocks.extend(self.nodes[0].generate(150))
        time.sleep(1)

        try:
            print "\nSC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))
        except JSONRPCException,e:
            errorString = e.error['message']
            print errorString

        print "\nSC info:\n", pprint.pprint(self.nodes[1].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(self.nodes[2].getscinfo(scid))

        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        print "Node1 balance: ", self.nodes[1].getbalance("", 0)
        print "Node2 balance: ", self.nodes[2].getbalance("", 0)
        print "=================================================="
        print 

        print 
        for i in range(0, 3):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print "---"

if __name__ == '__main__':
    sc_cert_base().main()
