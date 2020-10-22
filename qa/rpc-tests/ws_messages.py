#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs,\
    get_epoch_data, \
    assert_false, assert_true
from test_framework.mc_test.mc_test import *
import os
import json
from decimal import Decimal
import threading
from websocket import create_connection
from websocket._exceptions import WebSocketConnectionClosedException
from test_framework.wsproxy import JSONWSException

DEBUG_MODE = 1
NUMB_OF_NODES = 1
EPOCH_LENGTH = 5
CERT_FEE = Decimal('0.00015')

def get_epoch_data(scid, node, epochLen):
    sc_creating_height = node.getscinfo(scid)['created at block height']
    current_height = node.getblockcount()
    epoch_number = (current_height - sc_creating_height + 1) // epochLen - 1
    epoch_block_hash = node.getblockhash(sc_creating_height - 1 + ((epoch_number + 1) * epochLen))
    prev_epoch_block_hash = node.getblockhash(sc_creating_height - 1 + ((epoch_number) * epochLen))
    return epoch_block_hash, epoch_number, prev_epoch_block_hash


def ws_client(node, arg):
    wsurl = node.get_wsurl()

    if wsurl == None:
        print "###################### no ws conn: exiting"
        return

    print "##### ws client connecting to ws_url {} ######################".format(wsurl)
    ws = create_connection(wsurl)

    t = threading.currentThread()
    c = 0

    while getattr(t, "do_run", True):
        try:
            data = ws.recv()
            #c += 1
            #print "received data....", c

            if getattr(t, "handle_events", True):
                arg.wsEventPayload = json.loads(data)['eventPayload']
                arg.sem.release()
                print "############ Sem Given"
        except WebSocketConnectionClosedException, e:
            print "############ Server closed connection"
            break
        except Exception, e:
            print "Unexpected exception:  ", str(e)
            break


    print "##### ws client closing".format(wsurl)
    ws.close()

class ws_messages(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        common_args = [
            '-websocket=1', '-debug=ws',
            '-txindex=1',
            '-debug=py', '-debug=sc', '-debug=mempool', '-debug=net',
            '-debug=cert', '-debug=zendoo_mc_cryptolib', '-logtimemicros=1']

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args = [common_args]*NUMB_OF_NODES)

    def run_test(self):

        '''
        Different Websocket messages are tested
        '''

        self.sem = threading.Semaphore()
        self.sem.acquire()
        print "############ Sem Taken"

        t = threading.Thread(target=ws_client, args=(self.nodes[0], self,))
        t.daemon = True         # This thread dies when main thread exits.
        t.handle_events = False # do not handle evt by default
        t.start()

        self.nodes[0].getblockhash(0)

        mark_logs("Node 0 generates 220 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(220)

        mark_logs("Test for retrieving 1 header", self.nodes, DEBUG_MODE)
        start_height = self.nodes[0].getblockcount()
        self.nodes[0].generate(1)
        height = self.nodes[0].getblockcount()
        hashes = [self.nodes[0].getblockhash(n) for n in range(start_height, height)]
        self.nodes[0].ws_get_block_headers(hashes)

        mark_logs("Test for retrieving 10 headers", self.nodes, DEBUG_MODE)
        start_height = self.nodes[0].getblockcount()
        self.nodes[0].generate(10)
        height = self.nodes[0].getblockcount()
        hashes = [self.nodes[0].getblockhash(n) for n in range(start_height, height)]
        self.nodes[0].ws_get_block_headers(hashes)

        mark_logs("Test for retrieving 50 headers", self.nodes, DEBUG_MODE)
        start_height = self.nodes[0].getblockcount()
        self.nodes[0].generate(50)
        height = self.nodes[0].getblockcount()
        hashes = [self.nodes[0].getblockhash(n) for n in range(start_height, height)]
        self.nodes[0].ws_get_block_headers(hashes)

        mark_logs("Test for retrieving 51 headers(Should end up with exception: Invalid parameter)", self.nodes, DEBUG_MODE)
        start_height = self.nodes[0].getblockcount()
        self.nodes[0].generate(51)
        height = self.nodes[0].getblockcount()
        hashes = [self.nodes[0].getblockhash(n) for n in range(start_height, height)]
        try:
            self.nodes[0].ws_get_block_headers(hashes)
            print("We should never get here.")
        except JSONWSException as e:
            print "Exception:", e.error
        except Exception as e:
            print "Unexpected exception:  ", str(e)

if __name__ == '__main__':
    ws_messages().main()
