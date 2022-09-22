#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import os
import pprint
from decimal import Decimal
import time

from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import MINIMAL_SC_HEIGHT
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_false, assert_true, assert_equal, initialize_chain_clean, \
    start_nodes, start_node, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
    wait_bitcoinds, stop_nodes, \
    get_epoch_data, swap_bytes, get_spendable
from test_framework.mc_test.mc_test import CertTestUtils, generate_random_field_element_hex
from test_framework.mininode import COIN

NUMB_OF_NODES = 4
DEBUG_MODE = 1
EPOCH_LENGTH = 20
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal("0.00025")
CUSTOM_FEE_RATE_ZAT_PER_BYTE = Decimal('20.0')
CUSTOM_FEE_RATE_ZEN_PER_KBYTE = CUSTOM_FEE_RATE_ZAT_PER_BYTE/COIN*1000

class ScCertDust(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):

        self.nodes = start_nodes(NUMB_OF_NODES-1, self.options.tmpdir,
            extra_args=[
                ['-logtimemicros=1', '-debug=cert', '-debug=sc', '-debug=py', '-debug=mempool', '-allowdustoutput=0'],
                ['-logtimemicros=1', '-debug=cert', '-debug=sc', '-debug=py', '-debug=mempool', '-allowdustoutput=0'],
                ['-logtimemicros=1', '-debug=cert', '-debug=sc', '-debug=py', '-debug=mempool', '-allowdustoutput=0']
                ])

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        # do not connect node 3 at startup

        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        Test that a backward transfer amount under its dust threshold is not accepted in the mempool.
        Since this dust threshold depends on minrelaytxfee and this is an optional zend flag, test that 
        a node with a different value set behaves correctly and the network is not affected (no forks happen)
        '''

        mark_logs("Node 1 generates 2 block",self.nodes,DEBUG_MODE)
        self.nodes[1].generate(2)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(MINIMAL_SC_HEIGHT-2),self.nodes,DEBUG_MODE)
        self.nodes[0].generate(MINIMAL_SC_HEIGHT-2)
        self.sync_all()

        #generate wCertVk and constant
        mc_test = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mc_test.generate_params('sc1')
        constant = generate_random_field_element_hex()

        # create SC
        #------------------------------------------------------------------------------------------------------------
        cmd_input = {
            'version': 0,
            'toaddress': "abcd",
            'amount': 1.0,
            'wCertVk': vk,
            'withdrawalEpochLength': EPOCH_LENGTH,
            'constant': constant
        }

        mark_logs("\nNode 1 create SC", self.nodes, DEBUG_MODE)
        try:
            res = self.nodes[1].sc_create(cmd_input)
            scid = res['scid']
            pprint.pprint(res)
            self.sync_all()
        except JSONRPCException as e:
            error_string = e.error['message']
            mark_logs(error_string,self.nodes,DEBUG_MODE)
            assert_true(False)

        mark_logs("\nNode 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        scid_swapped = str(swap_bytes(scid))
        addr_node2   = self.nodes[2].getnewaddress()

        # this amount is next to the border of the dust (54 zat, according to mintxrelayfee default value) but it is sufficiently big not to be refused
        bwt_amount = Decimal('0.00000060')
        bwt_cert = [{"address": addr_node2, "amount": bwt_amount}, {"address": addr_node2, "amount": bwt_amount}]
        bwt_amount_array = [bwt_amount, bwt_amount]
        addr_array = [addr_node2, addr_node2]
        q = 10

        bl_list = []
        # advance some epoch and send a small backward transfer via a certificate for any epoch
        for i in range(3):

            if i == 1:
                # On the second loop, connect a fourth node from scratch with a greater mintxrelayfee, which would make the
                # node mempool reject the cert, and check this option does not prevent the chain update anyway
                mark_logs("Connecting a new Node3 with a greater -mintxrelayfee", self.nodes, DEBUG_MODE)
                self.nodes.append(start_node(
                    3, self.options.tmpdir , extra_args=[
                        '-logtimemicros=1', '-debug=cert', '-debug=sc', '-debug=py', '-debug=mempool',
                        '-allowdustoutput=0', '-minrelaytxfee='+str(CUSTOM_FEE_RATE_ZEN_PER_KBYTE)]))

                connect_nodes_bi(self.nodes, 2, 3)
                self.sync_all()

            mark_logs("\nAdvance epoch...", self.nodes, DEBUG_MODE)
            self.nodes[0].generate(EPOCH_LENGTH - 1)
            self.sync_all()
            epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

            mark_logs("Node 1 sends a cert with a bwd transfers of {} coins to Node2".format(bwt_amount), self.nodes, DEBUG_MODE)
            #==============================================================
            proof = mc_test.create_test_proof(
                "sc1", scid_swapped, epoch_number, q, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash,
                constant, addr_array, bwt_amount_array)

            try:
                cert = self.nodes[1].sc_send_certificate(scid, epoch_number, q,
                    epoch_cum_tree_hash, proof, bwt_cert, FT_SC_FEE, MBTR_SC_FEE)
            except JSONRPCException as e:
                error_string = e.error['message']
                print("Send certificate failed with reason {}".format(error_string))
                assert_true(False)

            sync_blocks(self.nodes[0:2])
            sync_mempools(self.nodes[0:2])

            mark_logs("cert = {}".format(cert), self.nodes, DEBUG_MODE)

            if i == 1:
                # check that the certificate has ben accepted by Node0 but not by Node3 wich has a greater mintxrelayfee
                mp0 = self.nodes[0].getrawmempool()
                mp3 = self.nodes[3].getrawmempool()
                assert_true(cert in mp0)
                assert_false(cert in mp3)

            mark_logs("\nNode 0 generates 1 block", self.nodes, DEBUG_MODE)
            bl_last = self.nodes[0].generate(1)[-1]
            bl_list.append(bl_last)
            self.sync_all()

        # dust amont for a cert backward transfer is 54 Zat using the 100 Zat/Kbyte default mintxrelayfee rate
        # check that no certificates with dust amount can be sent via any command type
        dust_amount = Decimal("0.00000053")
        bwt_cert = [{"address": addr_node2, "amount": dust_amount}]
        bwt_amount_array = [dust_amount]
        addr_array = [addr_node2]
        quality = 0
        proof = mc_test.create_test_proof(
            "sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE,
            epoch_cum_tree_hash, constant, addr_array, bwt_amount_array)

        utx, change = get_spendable(self.nodes[0], CERT_FEE)
        raw_inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        raw_outs    = { self.nodes[0].getnewaddress() : change }
        raw_bwt_outs = {addr_node2: dust_amount}

        raw_params = {
            "scid": scid,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash,
            "scProof": proof,
            "withdrawalEpochNumber": epoch_number
        }
        raw_cert = []
        cert = []

        try:
            raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[0].signrawtransaction(raw_cert)
            mark_logs("Node 0 sends a raw cert with a bwd transfers of {} coins to Node2 ... expecting failure".format(dust_amount), self.nodes, DEBUG_MODE)
            cert = self.nodes[0].sendrawtransaction(signed_cert['hex'])
            assert False
        except JSONRPCException as e:
            error_string = e.error['message']
            print("======> " + error_string)

        try:
            mark_logs("Node 0 sends a cert with a bwd transfers of {} coins to Node2 ... expecting failure".format(dust_amount), self.nodes, DEBUG_MODE)
            cert = self.nodes[0].sc_send_certificate(scid, epoch_number, q,
                epoch_cum_tree_hash, proof, bwt_cert, FT_SC_FEE, MBTR_SC_FEE)
            assert False
        except JSONRPCException as e:
            error_string = e.error['message']
            print("======> " + error_string)

        bal = self.nodes[2].getbalance()
        utx = self.nodes[2].listunspent()
        print("Node2 balance = {}".format(bal))
        assert_equal(bal, 2*bwt_amount)

        # the dust threshold for a bwt (54 Zat) is lower than the one for a standard output due to the replay protection
        # extension in the pub script (63 Zat). As a result we can not spend exactly one UTXOs coming from backward transfer
        # otherwise we would create a dust output.
        mark_logs("Node2 tries to spent one utxo from bwt sending {} coins to Node0 ... expecting failure".format(utx[0]['amount']), self.nodes, DEBUG_MODE)
        # try spending one utxo
        inputs  = [ {'txid' : utx[0]['txid'], 'vout' : utx[0]['vout']}]
        outputs = { self.nodes[0].getnewaddress() : utx[0]['amount'] }
        rawtx   = self.nodes[2].createrawtransaction(inputs, outputs)
        rawtx   = self.nodes[2].signrawtransaction(rawtx)

        error_string = ""
        try:
            rawtx   = self.nodes[2].sendrawtransaction(rawtx['hex'])
            assert False
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)

        # we can spend a pair of them instead
        mark_logs("Node2 tries to spent both utxo from bwt sending {} coins to Node0".format(bal), self.nodes, DEBUG_MODE)
        # try spending two utxos
        inputs  = [ {'txid' : utx[0]['txid'], 'vout' : utx[0]['vout']}, {'txid' : utx[1]['txid'], 'vout' : utx[1]['vout']}]
        outputs = { self.nodes[0].getnewaddress() : bal }
        rawtx   = self.nodes[2].createrawtransaction(inputs, outputs)
        rawtx   = self.nodes[2].signrawtransaction(rawtx)

        error_string = ""
        try:
            rawtx   = self.nodes[2].sendrawtransaction(rawtx['hex'])
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            assert False

        print("tx = {}".format(rawtx))

        sync_blocks(self.nodes[0:2])
        sync_mempools(self.nodes[0:2])
        # just to be sure tx is propagated
        time.sleep(2)

        mark_logs("Node 2 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[2].generate(1)
        self.sync_all()

        mark_logs("\nChecking persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

if __name__ == '__main__':
    ScCertDust().main()
