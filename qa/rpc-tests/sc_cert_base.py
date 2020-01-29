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
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-logtimemicros=1']] * 3 )

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
        fwt_amount = Decimal("50")
        bwt_amount_bad = Decimal("100.0")
        bwt_amount = Decimal("50")

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

        self.mark_logs("\nNode 1 creates the SC spending "+str(creation_amount)+" coins ...")
        amounts = []
        amounts.append( {"address":"dada", "amount": creation_amount})
        creating_tx = self.nodes[1].sc_create(scid, EPOCH_LENGTH, amounts);
        print "creating_tx = " + creating_tx
        self.sync_all()

        self.mark_logs("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        sc_creating_block = blocks[-1]
        sc_creating_height = self.nodes[0].getblockcount()
        self.sync_all()

        # fee can be seen on sender wallet (is a negative value)
        fee = self.nodes[1].gettransaction(creating_tx)['fee']
        print "Fee = ",fee

        # node 1 has just the coinbase minus the sc creation amount
        assert_equal(self.nodes[1].getbalance("", 0) + creation_amount - fee, bal_before) 
        print "\nNode1 balance: ", self.nodes[1].getbalance("", 0)

        self.mark_logs("\nNode 0 performs a fwd transfer of " + str(fwt_amount) + " coins to SC...")

        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount, scid);
        print "fwd_tx=" + fwd_tx
        self.sync_all()

        self.mark_logs("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        print "\nSC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))

        self.mark_logs("\nNode0 generating 3 more blocks for achieving sc coins maturity")
        blocks.extend(self.nodes[0].generate(3))
        self.sync_all()

        print "\nSC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))

        self.mark_logs("\nNode0 generating 1 more blocks for achieving end epoch")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        print "\nSC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))

        current_height = self.nodes[0].getblockcount()

        epn = int((current_height - sc_creating_height) / EPOCH_LENGTH)
        print " h=", current_height
        print "ch=", sc_creating_height
        print "epn=", epn
        eph = self.nodes[0].getblockhash(sc_creating_height + (epn*EPOCH_LENGTH))
        eph_wrong = self.nodes[0].getblockhash(sc_creating_height)
        print "epn = ", epn, ", eph = ", eph

        pkh_node1 = self.nodes[1].getnewaddress("", True);
        amounts = []
        cert_bad = []

        #----------------------------------------------------------------
        self.mark_logs("\nNode 0 tries to perform a bwd transfer with insufficient sc balance...")
        amounts.append( {"pubkeyhash":pkh_node1, "amount": bwt_amount_bad})
     
        # check this is refused because sc has not balance enough
        try:
            cert_bad = self.nodes[0].send_certificate(scid, epn, eph, amounts);
            assert(False)
        except JSONRPCException,e:
            errorString = e.error['message']
            print "\n======> ", errorString
            print

        amounts = []
        cert_bad = []
        cert_good = []

        print "\nSC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))

        self.mark_logs("\nNode 0 performs a bwd transfer with an invalid epoch number ...")
        amounts.append( {"pubkeyhash":pkh_node1, "amount": bwt_amount})
     
        # check this is refused because epoch number is wrong
        try:
            cert_bad = self.nodes[0].send_certificate(scid, epn+1, eph, amounts);
            assert(False)
        except JSONRPCException,e:
            errorString = e.error['message']
            print "\n======> ", errorString
            print

        self.mark_logs("\nNode 0 performs a bwd transfer with an invalid end epoch hash block ...")
        # check this is refused because end epoch block hash is wrong
        try:
            cert_bad = self.nodes[0].send_certificate(scid, epn, eph_wrong, amounts);
            assert(False)
        except JSONRPCException,e:
            errorString = e.error['message']
            print "\n======> ", errorString
            print

        self.mark_logs("\nNode 0 performs a bwd transfer of "+str(bwt_amount)+" coins to Node1 pkh["+str(pkh_node1)+"]...")
     
        try:
            cert_good = self.nodes[0].send_certificate(scid, epn, eph, amounts);
            print "cert = ", cert_good
            print "...OK"
        except JSONRPCException,e:
            errorString = e.error['message']
            print errorString
            assert(False)

        self.sync_all()

        print "\nChecking mempools..."
        print "Node 0: ", self.nodes[0].getrawmempool()
        print "Node 1: ", self.nodes[1].getrawmempool()
        print "Node 2: ", self.nodes[2].getrawmempool()

        bal_before = self.nodes[1].getbalance("", 0)
        print "\nNode1 balance: ", bal_before

        self.mark_logs("\nNode 0 performs a bwd transfer for the same epoch number as before before generating any block...")
        # check this is refused because this epoch already has a certificate in mempool
        try:
            cert_bad = self.nodes[0].send_certificate(scid, epn, eph, amounts);
            print "cert = ", cert_bad
            assert(False)
        except JSONRPCException,e:
            errorString = e.error['message']
            print "\n======> ", errorString
            print

        print("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        self.mark_logs("\nNode 0 performs a bwd transfer for the same epoch number as before...")
        # check this is refused because this epoch already has a certificate in sc info
        try:
            cert_bad = self.nodes[0].send_certificate(scid, epn, eph, amounts);
            print "cert = ", cert_bad
            assert(False)
        except JSONRPCException,e:
            errorString = e.error['message']
            print "\n======> ", errorString
            print

        # read the net value of the certificate amount (total amount - fee) on the receiver wallet
        cert_net_amount = self.nodes[1].gettransaction(cert_good)['amount']
        print "Cert net amount: ", cert_net_amount
        print

        bal_after = self.nodes[1].getbalance("", 0)
        assert_equal(bal_after, bal_before + cert_net_amount) 
        print "OK, Node1 balance has received the certificate net amount: ", bal_after
        print

        bal_before = bal_after

        # now Node1 use the UTXO related to certificate for sending coins to Node2
        self.mark_logs("\nNode 1 sends "+str(bwt_amount/2)+" coins to node2...")
        tx = self.nodes[1].sendtoaddress(self.nodes[2].getnewaddress(), bwt_amount/2)
        print "this tx uses certificate as input:"
        print "tx = ", tx
        print

        # fee can be seen on sender wallet
        fee_node2 = self.nodes[1].gettransaction(tx)['fee']
        print "fee: ", fee_node2
        print

        # check that input is formed using the certificate
        vin = self.nodes[1].getrawtransaction(tx, 1)['vin']
        assert_equal(vin[0]['txid'], cert_good)
        self.sync_all()

        print("\nNode0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        bal_after = self.nodes[1].getbalance("", 0)

        assert_equal(bal_after, bal_before - (bwt_amount/2) + fee_node2 ) 
        print "OK, Node1 balance has spent the amount and has been charged with the fee: ", self.nodes[1].getbalance("", 0)

        assert_equal(self.nodes[2].getbalance("", 0), (bwt_amount/2) ) 
        print "OK, Node2 balance has received the amount spent by Node1: ", self.nodes[2].getbalance("", 0)
        print



if __name__ == '__main__':
    sc_cert_base().main()
