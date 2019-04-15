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
class headers(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 2)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(2, self.options.tmpdir)

        if not split:
            connect_nodes_bi(self.nodes, 0, 1)
            sync_blocks(self.nodes[0:2])
            sync_mempools(self.nodes[0:2])

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
        self.disconnect_nodes(self.nodes[0], 1)
        self.disconnect_nodes(self.nodes[1], 0)
        self.is_network_split = True


    def join_network(self):
        #Join the (previously split) network halves together.
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 0)
        self.sync_all()
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

    def qqq(self, blocks, SHIELD_TYPE):

        arr = []
        arr = self.nodes[0].listaddressgroupings() 
        t_addr_g    = (arr[0][0][0])                                                                                            
#        pprint.pprint(arr)
        print
        height = self.nodes[0].getblockcount()
        print "\nChain height: ", height
        print "==========================================================\n"

        print "Testing T to T..."
        print "==================="
        t_addr = self.nodes[1].getnewaddress()
        print "...sending 1.0 coins from node0 to node1...\n"
        tx1 = self.nodes[0].sendtoaddress(t_addr, 1.0)
        res = self.nodes[0].getrawtransaction(tx1, 1)
        ver = res['version']
        print "  txid: ", tx1
        print "  Version: ", ver

        print "\nNode0 generating one block...\n"
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        print "Testing T to Z..."
        print "==================="
        z_addr = self.nodes[1].z_getnewaddress(SHIELD_TYPE)

        op_res = self.nodes[0].z_shieldcoinbase("*", z_addr, 0.0001, 1)
        opid = op_res['opid']
        amount = op_res['shieldingValue']
        print "...shielding %s coin base from node0 to node1" % amount
        arr = []
        arr.append(opid)

        while True:
            res = self.nodes[0].z_getoperationresult(arr) 
            if (len(res) > 0):
                break
            time.sleep(1)

        print "shield op completed after ", res[0]['execution_secs'], " secs"
        print

        tx_id = res[0]['result']['txid']

        res = self.nodes[0].getrawtransaction(tx_id, 1)
        ver = res['version']
        print "  txid: ", tx_id
        print "  Version: ", ver

        print "\nNode0 generating one block...\n"
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        print "Testing Z to T..."
        print "==================="
        t_addr = self.nodes[0].getnewaddress()

        recipients = []
        recipients.append({"address":t_addr, "amount": Decimal(1.0)})
        print "...sending 1.0 shielded coins from node1 to transparent node0 %s " % t_addr
        opid = self.nodes[1].z_sendmany(z_addr, recipients)

        arr = []
        arr.append(opid)

        while True:
            res = self.nodes[1].z_getoperationresult(arr) 
            if (len(res) > 0):
                break
            time.sleep(5)

        #print res
        print "shield op completed after ", res[0]['execution_secs'], " secs"
        print
        tx_id = res[0]['result']['txid']

        res = self.nodes[0].getrawtransaction(tx_id, 1)
        ver = res['version']
        print "  txid: ", tx_id
        print "  Version: ", ver
        print

        print "\nNode0 generating one block...\n"
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        print "Testing Z to Z..."
        print "==================="
        z_addr2 = self.nodes[0].z_getnewaddress(SHIELD_TYPE)

        recipients = []
        recipients.append({"address":z_addr2, "amount": Decimal(1.0)})
        print "...sending 1.0 shielded coins from node1 to shielded addr node0 %s " % t_addr
        opid = self.nodes[1].z_sendmany(z_addr, recipients)

        arr = []
        arr.append(opid)

        while True:
            res = self.nodes[1].z_getoperationresult(arr) 
            if (len(res) > 0):
                break
            time.sleep(5)

        #print res
        print "shield op completed after ", res[0]['execution_secs'], " secs"
        print
        tx_id = res[0]['result']['txid']

        res = self.nodes[0].getrawtransaction(tx_id, 1)
        ver = res['version']
        print "  txid: ", tx_id
        print "  Version: ", ver

        print "\nNode0 balance:"
        print self.nodes[0].z_gettotalbalance()
        
        print "\nNode1 balance:"
        print self.nodes[1].z_gettotalbalance()
        

    def run_test(self):
        blocks = []
        arr = []
        self.bl_count = 0

        blocks.append(self.nodes[0].getblockhash(0))

        # generate some coin base
        print "\nNode0 generates 101 blocks..."
        blocks.extend(self.nodes[0].generate(101))
        self.sync_all()

        self.qqq(blocks, "sprout")

        print "\nNode0 generates 100 blocks..."
        blocks.extend(self.nodes[0].generate(100))
        self.sync_all()

        self.qqq(blocks, "sprout")

        print "\nNode0 generates 20 blocks..."
        blocks.extend(self.nodes[0].generate(20))
        self.sync_all()

        self.qqq(blocks, "sprout")

        print "\nNode0 generates 10 blocks..."
        blocks.extend(self.nodes[0].generate(10))
        self.sync_all()

        self.qqq(blocks, "sapling")



if __name__ == '__main__':
    headers().main()
