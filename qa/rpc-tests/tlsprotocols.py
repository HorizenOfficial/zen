#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_true, initialize_chain_clean, \
    start_nodes, sync_blocks, connect_nodes_bi, p2p_port
import os,sys
import socket
import ssl
import time

NUMB_OF_NODES = 2

class tlsproto(BitcoinTestFramework):

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
                ["-logtimemicros", "-debug=net", "-debug=tls", "-debug=py", "-tlsfallbacknontls=0"]
            ])

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 0)

        self.is_network_split = split
        self.sync_all()

    def mark_logs(self, msg):
        print(msg)
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)


    def run_test(self):

        def do_tls_conn(tls_protocol, peer, expected_result=True, ciphers=None, tlsOnly=False):

            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(1)
 
            ctx = ssl.SSLContext(tls_protocol)
            if ciphers:
                print("Trying ciphers: ", ciphers)
                try:
                    ctx.set_ciphers(ciphers)
                except Exception as e:
                    print("===> Error: could not set ciphers {}, {}".format(ciphers, e))
                    print("     Check openssl version used by python")
                    return

            if expected_result == False:
                res_string = "should fail"
            else:
                res_string = "should succeed"
            print("--->", res_string)

            result = True

            try:
                wrappedSocket = ctx.wrap_socket(sock)
                wrappedSocket.connect(peer)
                if expected_result == result:
                    print("...OK, TLS connection established")
                print("negotiated protocol: {}, using cipher: {}".format(wrappedSocket.version(), wrappedSocket.cipher()[0]))
                wrappedSocket.shutdown(socket.SHUT_RDWR)
                wrappedSocket.close()
                sock.close()
            except Exception as e:
                result = False
                if expected_result != result:
                    print("Error: ", e)
                    assert_true(False)

                print("...OK, TLS connection failed: ", e)

                wrappedSocket.close()
                sock.close()
 
                if tlsOnly == False:
                    # upon failure, a TLS node optionally can fall back to unencrypted mode for this endpoint ip, 
                    # therefore we must connect/disconnect in order to be able to try a new TLS connection 
                    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    sock.settimeout(1)
                    print("...connecting non-TLS and exiting...")
                    sock.connect(peer)
                    sock.close()

            if result != expected_result:
                raise AssertionError("Unexpected result!")
            print

 

        # this api is supported only from py 2.7.15
        #b = ssl.HAS_TLSv1_3

        # TLS1.3 support has been added from 1.1.1 on
        openssl_111_v = hex(int('1010100f', base=16))

        hex_openssl_v = hex(ssl.OPENSSL_VERSION_NUMBER)
        print("Using system lib: {} - ({})".format(ssl.OPENSSL_VERSION, hex_openssl_v))

        # generate some block and sync, just in case
        self.nodes[1].generate(10)
        self.sync_all()

        # connect on Node0
        HOST, PORT = '127.0.0.1', p2p_port(0)

        self.mark_logs("\nTrying TLSv1 connection")
        do_tls_conn(ssl.PROTOCOL_TLSv1, (HOST, PORT), expected_result=False)

        self.mark_logs("\nTrying TLSv1_1 connection")
        do_tls_conn(ssl.PROTOCOL_TLSv1_1, (HOST, PORT), expected_result=False)

        self.mark_logs("\nTrying TLSv1_2 connection with non-PFS cipher")
        do_tls_conn(ssl.PROTOCOL_TLSv1_2, (HOST, PORT), expected_result=False, ciphers="AES256-GCM-SHA384")

        self.mark_logs("\nTrying TLSv1_2 connection with several PFS ciphers...\n")

        # TLS1.2 ciphers supporting PFS and using RSA autentication
        ciph_array = [
            "ECDHE-RSA-AES256-GCM-SHA384",
            "DHE-RSA-AES256-GCM-SHA384",
            "ECDHE-RSA-AES128-GCM-SHA256",
            "DHE-RSA-AES128-GCM-SHA256"
        ]

        for c in ciph_array:
            do_tls_conn(ssl.PROTOCOL_TLSv1_2, (HOST, PORT), expected_result=True, ciphers=c)

        # connect to Node1 which supports only TLS connections (no fallback to unencripted connections)
        HOST, PORT = '127.0.0.1', p2p_port(1)

        self.mark_logs("\nTrying TLSv1 connection with tls-only node")
        do_tls_conn(ssl.PROTOCOL_TLSv1, (HOST, PORT), expected_result=False, tlsOnly=True)

        self.mark_logs("\nTrying TLSv1_2 connection with tls-only node with unsupported cipher")
        do_tls_conn(ssl.PROTOCOL_TLSv1_2, (HOST, PORT), expected_result=False, ciphers="AES128-GCM-SHA256", tlsOnly=True)

        self.mark_logs("\nTrying TLSv1_2 connection with tls-only node with supported cipher")
        do_tls_conn(ssl.PROTOCOL_TLSv1_2, (HOST, PORT), expected_result=True, ciphers="DHE-RSA-AES256-GCM-SHA384", tlsOnly=True)

        self.mark_logs("\nTrying TLSv1_2 connection with tls-only node letting it choose cipher")
        do_tls_conn(ssl.PROTOCOL_TLSv1_2, (HOST, PORT), expected_result=True, ciphers=None, tlsOnly=True)
 
        if hex_openssl_v >= openssl_111_v:
            self.mark_logs("\nTrying TLSv1_3 connection with tls-only node letting it choose cipher")
            do_tls_conn(ssl.PROTOCOL_TLS, (HOST, PORT), expected_result=True, ciphers=None, tlsOnly=True)
        else:
            print("No test with TLS1.3 can be done since client does not support it, at least OpenSSL 1.1.1 ({}) is necessary\n".format(str(openssl_111_v)))
 


if __name__ == '__main__':
    tlsproto().main()
