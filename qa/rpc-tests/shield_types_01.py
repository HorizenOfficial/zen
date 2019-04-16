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

NUMB_OF_NODES = 3

class headers(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir)

        if not split:
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:NUMB_OF_NODES])
            sync_mempools(self.nodes[1:NUMB_OF_NODES])

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
        # Split the network of two nodes into nodes 0 and 1.
        assert not self.is_network_split
        self.disconnect_nodes(self.nodes[1], 2)
        self.disconnect_nodes(self.nodes[2], 1)
        self.is_network_split = True


    def join_network(self):
        #Join the (previously split) network halves together.
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 1)
        self.sync_all()
        self.is_network_split = False

    def dump_fmt(self, node_s, node_r, addr_s, addr_r, amount):
        print "-------------------------------------------------------"
        print "    Node%d [%s]" % (node_s, addr_s)
        print "      |"
        print "     %f" % amount
        print "      |"
        print "      V"
        print "    Node%d [%s]" % (node_r, addr_r)

    def do_transactions(self, blocks, SHIELD_TYPE):

        height = self.nodes[0].getblockcount()
        print
        print "===================="
        print "  Chain height: ", height
        print "===================="
        print

        z_addr_1 = self.nodes[1].z_getnewaddress(SHIELD_TYPE)
        op_res = self.nodes[0].z_shieldcoinbase("*", z_addr_1, 0.0001, 1)
        opid = op_res['opid']
        amount = op_res['shieldingValue']
        print "Testing shield coin base T to Z(%s)" % (SHIELD_TYPE)
        self.dump_fmt(0, 1, "*", z_addr_1, amount)
#        print "    [%s] Node0 --- %f ---> Node1 [%s]" % ("\"*\"", Decimal(amount), z_addr_1)
        arr = []
        arr.append(opid)

        while True:
            res = self.nodes[0].z_getoperationresult(arr) 
            if (len(res) > 0):
                break
            time.sleep(3)

        print
        print "    ...op completed after ", res[0]['execution_secs'], " secs"
        print
        tx_id = res[0]['result']['txid']
        res = self.nodes[0].getrawtransaction(tx_id, 1)
        ver = res['version']
        print "    txid: ", tx_id
        print "    Version: ", ver
        print

        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        t_addr = self.nodes[1].getnewaddress()
        amount = 1.0
        print "Testing T to T"
        self.dump_fmt(0, 1, "*", t_addr, amount)
#        print "    [%s] Node0 --- %f ---> Node1 [%s] " % ("\"*\"", Decimal(amount), t_addr)
        tx1 = self.nodes[0].sendtoaddress(t_addr, 1.0)
        res = self.nodes[0].getrawtransaction(tx1, 1)
        ver = res['version']
        print
        print "    txid: ", tx1
        print "    Version: ", ver
        print
#        pprint.pprint(res)

        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        amount = 0.5
        z_addr_0 = self.nodes[0].z_getnewaddress(SHIELD_TYPE)
        recipients = []
        recipients.append({"address":z_addr_0, "amount": Decimal(amount)})
        print
        print "Testing T to Z(%s)" % (SHIELD_TYPE)
        self.dump_fmt(1, 0, t_addr, z_addr_0, amount)
#        print "    [%s] Node1 --- %f ---> Node0 [%s] " % (t_addr, Decimal(amount), z_addr_0)
        opid = self.nodes[1].z_sendmany(t_addr, recipients)

        arr = []
        arr.append(opid)

        while True:
            res = self.nodes[1].z_getoperationresult(arr) 
            if (len(res) > 0):
                break
            time.sleep(5)

        #print res
        print
        print "    ...op completed after ", res[0]['execution_secs'], " secs"
        print
        tx_id = res[0]['result']['txid']

        res = self.nodes[0].getrawtransaction(tx_id, 1)
        ver = res['version']
        print "    txid: ", tx_id
        print "    Version: ", ver
        print

        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        t_addr = self.nodes[0].getnewaddress()
        amount = 1.0
        recipients = []
        recipients.append({"address":t_addr, "amount": Decimal(amount)})
        print
        print "Testing Z(%s) to T" % (SHIELD_TYPE)
        self.dump_fmt(1, 0, z_addr_1, t_addr, amount)
#        print "    [%s] Node1 --- %f ---> Node0 [%s] " % (z_addr_1, Decimal(amount), t_addr)
        opid = self.nodes[1].z_sendmany(z_addr_1, recipients)

        arr = []
        arr.append(opid)

        while True:
            res = self.nodes[1].z_getoperationresult(arr) 
            if (len(res) > 0):
                break
            time.sleep(5)

        #print res
        print
        print "    ...op completed after ", res[0]['execution_secs'], " secs"
        print
        tx_id = res[0]['result']['txid']

        res = self.nodes[0].getrawtransaction(tx_id, 1)
        ver = res['version']
        print "    txid: ", tx_id
        print "    Version: ", ver
        print

        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        amount = 3.0
        recipients = []
        recipients.append({"address":z_addr_0, "amount": Decimal(amount)})
        print
        print "Testing Z(%s) to Z(%s)" % (SHIELD_TYPE, SHIELD_TYPE)
        self.dump_fmt(1, 0, z_addr_1, z_addr_0, amount)
#        print "    [%s] Node1 --- %f ---> Node0 [%s] " % (z_addr_1, Decimal(amount), z_addr_0)
        opid = self.nodes[1].z_sendmany(z_addr_1, recipients)

        arr = []
        arr.append(opid)

        while True:
            res = self.nodes[1].z_getoperationresult(arr) 
            if (len(res) > 0):
                break
            time.sleep(5)

        #print res
        print
        print "    ...op completed after ", res[0]['execution_secs'], " secs"
        print
        tx_id = res[0]['result']['txid']

        res = self.nodes[0].getrawtransaction(tx_id, 1)
        ver = res['version']
        print "    txid: ", tx_id
        print "    Version: ", ver
        print
        #pprint.pprint(res)
        print

        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        print "\nNode0 balance:"
        pprint.pprint(self.nodes[0].z_gettotalbalance())
        
        print "\nNode1 balance:"
        pprint.pprint(self.nodes[1].z_gettotalbalance())
        
    def do_bad_transactions(self, blocks):

        height = self.nodes[0].getblockcount()
        print
        print "===================="
        print "  Chain height: ", height
        print "===================="
        print

        amount = 3.0
        s_addr_1 = self.nodes[1].z_getnewaddress("sapling")
        z_addr_0 = self.nodes[0].z_getnewaddress("sprout")
        recipients = []
        recipients.append({"address":z_addr_0, "amount": Decimal(amount)})
        print
        print "Testing Z(%s) to Z(%s)" % ("sapling", "sprout")
        self.dump_fmt(1, 0, s_addr_1, z_addr_0, amount)
#        print "    [%s] Node1 --- %f ---> Node0 [%s] " % (s_addr_1, Decimal(amount), z_addr_0)
        try:
            opid = self.nodes[1].z_sendmany(s_addr_1, recipients)
        except JSONRPCException,e:
            errorString = e.error['message']
            print "============================================================================================="
            print "Failed:\n\t", errorString
            print "============================================================================================="
            print

        print "\nNode0 balance:"
        pprint.pprint(self.nodes[0].z_gettotalbalance())
        
        print "\nNode1 balance:"
        pprint.pprint(self.nodes[1].z_gettotalbalance())
        

    def run_test(self):
        blocks = []
        arr = []
        self.bl_count = 0

        blocks.append(self.nodes[0].getblockhash(0))

        # generate some coin base
        print "\nNode0 generates 101 blocks..."
        blocks.extend(self.nodes[0].generate(101))
        self.sync_all()

        print("\n\nSplit network")
        self.split_network()
        print("The network is split")

        try:
            self.do_transactions(blocks, "sprout")
 
            print "\nNode0 generates 96 blocks..."
            blocks.extend(self.nodes[0].generate(96))
            self.sync_all()
 
            self.do_transactions(blocks, "sprout")
 
            print "\nNode0 generates 16 blocks..."
            blocks.extend(self.nodes[0].generate(16))
            self.sync_all()
 
            self.do_transactions(blocks, "sprout")
 
            print "\nNode0 generates 6 blocks..."
            blocks.extend(self.nodes[0].generate(6))
            self.sync_all()
 
            self.do_transactions(blocks, "sprout")
            self.do_transactions(blocks, "sapling")

        except JSONRPCException,e:
            errorString = e.error['message']
            print "============================================================================================="
            print "Failed:\n\t", errorString
            print "============================================================================================="
            print

        self.do_bad_transactions(blocks)

        print("\n\nJoin network")
        self.join_network()
        time.sleep(5)
        print("\nNetwork joined") 


if __name__ == '__main__':
    headers().main()
