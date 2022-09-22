#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import MINIMAL_SC_HEIGHT
from test_framework.util import assert_true, assert_equal, initialize_chain_clean, \
    start_nodes, stop_nodes, wait_bitcoinds, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
    dump_sc_info, dump_sc_info_record, get_epoch_data, get_spendable, swap_bytes, advance_epoch
from test_framework.mc_test.mc_test import CertTestUtils, generate_random_field_element_hex
import os
import pprint
from decimal import Decimal
import json
import time

NUMB_OF_NODES = 2
DEBUG_MODE = 1
EPOCH_LENGTH = 40
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal("0.00015")


class sc_cert_bwt_amount_rounding(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[['-logtimemicros=1', '-debug=py', '-debug=sc', '-debug=mempool', '-debug=cert', '-debug=bench',
                                              '-scproofqueuesize=0']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        def create_sc(cmdInput, node):
            try:
                res = node.sc_create(cmdInput)
                tx =   res['txid']
                scid = res['scid']
            except JSONRPCException as e:
                errorString = e.error['message']
                mark_logs(errorString,self.nodes,DEBUG_MODE)
                assert_true(False)

            return tx, scid

        '''
        Purpose of this test is to verify that a decimal amount with many decimal digits is correctly handled, expecially
        with reference to the compatibility between creation and verification of the proof, which are performed by the test
        framework and the zend_oo core implementations.
        '''
        # network topology: (0)--(1)

        mark_logs("Node 1 generates {} block".format(2), self.nodes, DEBUG_MODE)
        self.nodes[1].generate(2)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(MINIMAL_SC_HEIGHT), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(MINIMAL_SC_HEIGHT-2)
        self.sync_all()

        #generate Vks and constant
        certMcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)

        certVk = certMcTest.generate_params('scs')
        constant = generate_random_field_element_hex()
        cr_amount = 1000.0

        #-------------------------------------------------------
        fee = 0.000025

        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': EPOCH_LENGTH,
            'amount': cr_amount,
            'fee': fee,
            'constant':constant,
            'wCertVk': certVk,
            'toaddress':"cdcd"
        }
      
        tx, scid = create_sc(cmdInput, self.nodes[0])
        mark_logs("Created SC with scid={} via tx={}".format(scid, tx), self.nodes,DEBUG_MODE)
        self.sync_all()
        hexTx = self.nodes[0].getrawtransaction(tx)
        print("sz=", len(hexTx)//2)

        # advance epoch
        self.nodes[0].generate(EPOCH_LENGTH)
        self.sync_all()
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        NUM_OF_BWT = 1
        print("===============================================================================================================================================================================")
        print("Adding {} backward transfers to certificate".format(NUM_OF_BWT))
        print("===============================================================================================================================================================================")

        bwt_amount = Decimal('1.952929687000111')

        print("bwt amount {}".format(bwt_amount))
        bwt_cert = []
        addr_array = []
        bwt_amount_array = []
        proof = None
        addr_node1 = self.nodes[1].getnewaddress()

        for _ in range(0, NUM_OF_BWT):

            addr_array.append(addr_node1)
            bwt_amount_array.append(bwt_amount)

            entry = {"address": addr_node1, "amount": bwt_amount}
            bwt_cert.append(entry)
            pprint.pprint(entry)

        print("Generating cert proof...")
        t0 = time.time()
        q = 10
        scid_swapped = str(swap_bytes(scid))
        print("---------------------")
        proof = certMcTest.create_test_proof(
            "scs", scid_swapped, epoch_number, q, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, addr_array, bwt_amount_array)
        assert_true(proof != None)
        t1 = time.time()
        print("...proof with sz={} generated: {} secs".format(len(proof)//2, t1-t0))
        
        try:
            cert = self.nodes[0].sc_send_certificate(scid, epoch_number, q,
                epoch_cum_tree_hash, proof, bwt_cert, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Send certificate failed with reason {}".format(errorString))
            assert(False)
        self.sync_all()
 
        mark_logs("cert={}".format(cert), self.nodes, DEBUG_MODE)
        hexCert = self.nodes[0].getrawtransaction(cert)
        tot_cert_sz = len(hexCert)//2
        print("sz=", tot_cert_sz)

        assert_true(cert in self.nodes[1].getrawmempool())

if __name__ == '__main__':
    sc_cert_bwt_amount_rounding().main()
