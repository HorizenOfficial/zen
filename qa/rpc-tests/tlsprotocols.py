#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.script import OP_DUP, OP_EQUALVERIFY, OP_HASH160, OP_EQUAL, hash160, OP_CHECKSIG, OP_CHECKBLOCKATHEIGHT
from test_framework.util import assert_true, assert_equal, assert_greater_than, initialize_chain_clean, \
    start_nodes, start_node, connect_nodes, stop_node, stop_nodes, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, p2p_port, check_json_precision
from test_framework.script import CScript
from test_framework.mininode import CTransaction, ToHex
from test_framework.util import hex_str_to_bytes, bytes_to_hex_str
import traceback
from binascii import unhexlify
import cStringIO
import os,sys
import shutil
from decimal import Decimal
import binascii
import codecs

import socket
import ssl
'''
from random import randint
import logging
import pprint
import struct
import array
'''

import time

NUMB_OF_NODES = 2

class tls01(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
            extra_args = [
                ["-logtimemicros", "-debug=net", "-debug=tls", "-debug=py"],
                ["-logtimemicros", "-debug=net", "-debug=tls", "-debug=py"]
            ])

        if not split:
            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 1, 0)

        self.is_network_split = split
        self.sync_all()
        '''
        self.is_network_split = split
        '''

    def disconnect_nodes(self, from_connection, node_num):
        ip_port = "127.0.0.1:"+str(p2p_port(node_num))
        from_connection.disconnectnode(ip_port)
        # poll until version handshake complete to avoid race conditions
        # with transaction relaying
        while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
            time.sleep(0.1)

    def split_network(self):
        # Split the network of 4 nodes into nodes 0-1-2 and 3.
        assert not self.is_network_split
        self.disconnect_nodes(self.nodes[0], 1)
        self.disconnect_nodes(self.nodes[1], 0)
        self.is_network_split = True


    def join_network(self):
        #Join the (previously split) network pieces together: 0-1-2-3
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 0)
        sync_blocks(self.nodes, 1, False, 5)
        self.is_network_split = False

    def mark_logs(self, msg):
        print msg
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)


    def run_test(self):

        def do_tls_conn(tls_protocol, peer, expected_result, ciphers=None):
            # CREATE SOCKET
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(1)
 
            ctx = ssl.SSLContext(tls_protocol)
            if ciphers:
                try:
                    ctx.set_ciphers(ciphers)
                except Exception, e:
                    print "Error: could not set ciphers {}, {}".format(ciphers, e)
                    assert_true(False)

            if expected_result == False:
                res_string = "should fail"
            else:
                res_string = "should succed"
            print "--->", res_string

            result = True

            try:
                wrappedSocket = ctx.wrap_socket(sock)
                wrappedSocket.connect(peer)
                if expected_result == result:
                    print "...OK, TLS connection established"
                print "negotiated protocol: {}, using cipher: {}".format(wrappedSocket.version(), wrappedSocket.cipher()[0])
                wrappedSocket.shutdown(socket.SHUT_RDWR)
                wrappedSocket.close()
                sock.close()
            except Exception, e:
                result = False
                if expected_result != result:
                    print "Error: ", e
                    assert_true(False)

                print "OK, it failed: ", e

                wrappedSocket.close()
                sock.close()
 
                # upon failure a TLS node falls back to unencrypted mode for this endpoint ip, 
                # therefore we must connect/disconnect in order to be able to try a new TLS connection 
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect(peer)
                sock.close()

            if result != expected_result:
                raise AssertionError("Unexpected result!")

 

        print "Using system lib: ", ssl.OPENSSL_VERSION
        self.nodes[1].generate(10)
        self.sync_all()

        HOST, PORT = '127.0.0.1', p2p_port(0)

        self.mark_logs("\nTrying TLSv1 connection")
        do_tls_conn(ssl.PROTOCOL_TLSv1, (HOST, PORT), False)

        self.mark_logs("\nTrying TLSv1_1 connection")
        do_tls_conn(ssl.PROTOCOL_TLSv1_1, (HOST, PORT), False)

        self.mark_logs("\nTrying TLSv1_2 connection with non-PFS cipher")
        do_tls_conn(ssl.PROTOCOL_TLSv1_2, (HOST, PORT), False, ciphers="AES256-GCM-SHA384")

        self.mark_logs("\nTrying TLSv1_2 connection with PFS ciphers")

        # TLS1.2 ciphers supporting PFS and using RSA autentication
        ciph_array = [
            "DHE-RSA-AES256-GCM-SHA384",
            "DHE-RSA-AES256-SHA256",
            "DHE-RSA-AES128-GCM-SHA256",
            "DHE-RSA-AES128-SHA256",
            "ECDHE-RSA-AES256-GCM-SHA384"
        ]

        for c in ciph_array:
            print "\n    ", c
            do_tls_conn(ssl.PROTOCOL_TLSv1_2, (HOST, PORT), True, ciphers=c)



if __name__ == '__main__':
    tls01().main()
