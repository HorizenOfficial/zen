#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import MINIMAL_SC_HEIGHT, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, get_epoch_data, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, swap_bytes
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal
import pprint
import time

DEBUG_MODE = 1
NUMB_OF_NODES = 4
EPOCH_LENGTH = 5
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')


class sc_cert_base(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib', '-scproofqueuesize=0', '-logtimemicros=1']] * NUMB_OF_NODES)

        for k in range(0, NUMB_OF_NODES-1):
            connect_nodes_bi(self.nodes, k, k+1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        Node1 creates a sc and sends to it a certificate with a bwt to Node2 and a fee.
        Node0 mines a block and checks that the cert fee is contained in the coinbase
        Node0 sends a lot of small coins to Node3, who will use them as input for including a fee
        in a new certificate
        '''

        # cross chain transfer amounts
        creation_amount = Decimal("0.5")
        bwt_amount = Decimal("0.4")

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(MINIMAL_SC_HEIGHT), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(MINIMAL_SC_HEIGHT)
        self.sync_all()

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir, "darlin")
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": constant,
        }

        ret = self.nodes[1].sc_create(cmdInput)

        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs("Node 1 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        prev_epoch_block_hash = self.nodes[0].getblockhash(self.nodes[0].getblockcount())
        self.nodes[0].generate(1)
        self.sync_all()

        # fee can be seen on sender wallet (it is a negative value)
        fee = self.nodes[1].gettransaction(creating_tx)['fee']
        mark_logs("Fee paid for SC creation: {}".format(fee), self.nodes, DEBUG_MODE)

        mark_logs("Node0 generating 4 more blocks to achieve end of withdrawal epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(4)
        self.sync_all()

        mark_logs("Sc {} state: {}".format(scid, self.nodes[0].getscinfo(scid)['items'][0]), self.nodes, DEBUG_MODE)

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        addr_node2 = self.nodes[2].getnewaddress()
        amounts = [{"address": addr_node2, "amount": bwt_amount}]
        

        #Create proof for WCert
        quality = 1
        proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node2], [bwt_amount])
        
        mark_logs("Node 1 performs a bwd transfer of {} coins to Node2 address {}".format(bwt_amount, addr_node2), self.nodes, DEBUG_MODE)
        try:
            cert_good = self.nodes[1].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_good) > 0)
            mark_logs("Certificate is {}".format(cert_good), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Checking mempools alignment", self.nodes, DEBUG_MODE)
        self.sync_all()
        for i in range(1, NUMB_OF_NODES):
            assert_equal(sorted(self.nodes[0].getrawmempool()), sorted(self.nodes[i].getrawmempool()))

        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_good in self.nodes[0].getrawmempool())

        mark_logs("Node0 confims bwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        mined = self.nodes[0].generate(1)[0]
        self.sync_all()

        mark_logs("Check cert is not in mempool anymore", self.nodes, DEBUG_MODE)
        assert_equal(False, cert_good in self.nodes[0].getrawmempool())

        mark_logs("Check block coinbase contains the certificate fee", self.nodes, DEBUG_MODE)
        coinbase = self.nodes[0].getblock(mined, True)['tx'][0]
        decoded_coinbase = self.nodes[2].getrawtransaction(coinbase, 1)
        miner_quota = decoded_coinbase['vout'][0]['value']
        assert_equal(miner_quota, (Decimal(MINER_REWARD_POST_H200) + CERT_FEE))

        mark_logs("Checking that amount transferred by certificate reaches Node2 wallet and it is immature", self.nodes, DEBUG_MODE)

        try:
            res = self.nodes[2].gettransaction(cert_good)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Get transaction failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        # Since the amount is immature, it should not be displayed by default by the GetTransaction command, the 'details' array must be empty
        assert(not res['details'])

        # Call GetTransaction once again by specifically requesting immature BT amounts
        includeWatchonly = False
        includeImmatureBTs = True

        try:
            res = self.nodes[2].gettransaction(cert_good, includeWatchonly, includeImmatureBTs)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Get transaction failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        cert_net_amount = res['details'][0]['amount']
        assert_equal(cert_net_amount, bwt_amount)

        # Node0 sends a lot of small coins to Node3, who will use as input for certificate fee
        taddr3 = self.nodes[3].getnewaddress()
        small_amount = Decimal("0.0000123")
        for n in range(0, 30):
            self.nodes[0].sendtoaddress(taddr3, small_amount)

        self.sync_all()

        mark_logs("Node 0 generates 4 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(4)
        self.sync_all()

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        bal3 = self.nodes[3].getbalance()
        bwt_amount_2 = bal3/2
        amounts = [{"address": addr_node2, "amount": bwt_amount_2}]

        #Create proof for WCert
        quality = 2
        proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node2], [bwt_amount_2])
        
        mark_logs("Node 3 performs a bwd transfer of {} coins to Node2 address {}".format(bwt_amount_2, addr_node2), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[3].sc_send_certificate(scid, epoch_number, quality, epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert) > 0)
            mark_logs("Certificate is {}".format(cert), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Node 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # check that Node2 has not yet matured the certificate balance received the previous epoch
        cert_net_amount = self.nodes[2].gettransaction(cert_good)['amount']
        assert_equal(cert_net_amount, Decimal('0.0'))

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        # check that Node2 has now achieved maturity for the certificate balance received the previous epoch
        # since we are past the safe guard
        cert_net_amount = self.nodes[2].gettransaction(cert_good)['amount']
        assert_equal(cert_net_amount, bwt_amount)


if __name__ == '__main__':
    sc_cert_base().main()
