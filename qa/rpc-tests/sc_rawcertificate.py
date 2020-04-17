#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, p2p_port, mark_logs
import os
from decimal import Decimal
import operator

import time

DEBUG_MODE = 1
EPOCH_LENGTH = 5
NUMB_OF_NODES = 4
CERT_FEE = 0.0001

class sc_rawcert(BitcoinTestFramework):

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
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-logtimemicros=1', '-txindex=1', '-zapwallettxes=2']] * NUMB_OF_NODES)

        for idx, _ in enumerate(self.nodes):
            if idx < (NUMB_OF_NODES - 1):
                connect_nodes_bi(self.nodes, idx, idx + 1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def disconnect_nodes(self, from_connection, node_num):
        ip_port = "127.0.0.1:" + str(p2p_port(node_num))
        from_connection.disconnectnode(ip_port)
        # poll until version handshake complete to avoid race conditions
        # with transaction relaying
        while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
            time.sleep(0.1)

    def run_test(self):

        # side chain id
        scid = "1111111111111111111111111111111111111111111111111111111111111111"

        # forward transfer amount
        cr_amount = Decimal("2.0")
        ft_amount = Decimal("3.0")
        bt_amount = Decimal("4.0")
        sc_amount = cr_amount + ft_amount

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates 220 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(220)
        self.sync_all()

        # node 1 has just the coinbase which is now mature
        bal_before = self.nodes[1].getbalance("", 0)

        # create a sc via createraw cmd
        mark_logs("Node 1 creates the SC spending " + str(sc_amount) + " coins ...", self.nodes, DEBUG_MODE)
        sc_address = "fade"
        sc_cr = [{"scid": scid, "epoch_length": EPOCH_LENGTH, "amount": cr_amount, "address": sc_address, "customData": "badcaffe"}]
        sc_ft = [{"address": sc_address, "amount":ft_amount, "scid": scid}]
        raw_tx = self.nodes[1].createrawtransaction([], {}, sc_cr, sc_ft)
        funded_tx = self.nodes[1].fundrawtransaction(raw_tx)
        signed_tx = self.nodes[1].signrawtransaction(funded_tx['hex'])
        creating_tx = self.nodes[1].sendrawtransaction(signed_tx['hex'])
        self.sync_all()

        mark_logs("Node0 generating 5 block", self.nodes, DEBUG_MODE)
        epn = 0
        eph = self.nodes[0].generate(EPOCH_LENGTH)[-1]
        self.sync_all()

        # -------------------------- end epoch

        sc_funds_pre = self.nodes[3].getscinfo(scid)['balance']

        pkh_node2 = self.nodes[2].getnewaddress("", True)

        mark_logs("Node0 generating 2 block, overcoming safeguard", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(2)
        self.sync_all()

        raw_addresses = {pkh_node2: bt_amount}
        raw_params = {"scid": scid, "endEpochBlockHash": eph, "fee": CERT_FEE, "withdrawalEpochNumber": epn}
        raw_cert = []
        cert = []

        try:
            raw_cert = self.nodes[0].createrawcertificate(raw_addresses, raw_params)
        except JSONRPCException, e:
            errorString = e.error['message']
            print "\n======> ", errorString
            assert_true(False)

        decoded_cert_pre = self.nodes[0].decoderawcertificate(raw_cert)
        decoded_cert_pre_list = sorted(decoded_cert_pre.items())

        mark_logs("Node0 sending raw certificate for epoch {}, expecting failure...".format(epn), self.nodes, DEBUG_MODE)
        # we expect it to fail because beyond the safeguard
        try:
            cert = self.nodes[0].sendrawcertificate(raw_cert)
            assert_true(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            print "\n======> ", errorString

        mark_logs("Node0 invalidates last block, thus shortening the chain by one and returning in the safe margin", self.nodes, DEBUG_MODE)
        self.nodes[0].invalidateblock(self.nodes[0].getbestblockhash())
        sync_mempools(self.nodes[1:3])

        mark_logs("Node0 sending raw certificate for epoch {}, expecting success".format(epn), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[0].sendrawcertificate(raw_cert)
        except JSONRPCException, e:
            errorString = e.error['message']
            print "\n======> ", errorString
            assert_true(False)

        sync_mempools(self.nodes[1:3])

        mark_logs("Node0 generating 4 block, also reverting other chains", self.nodes, DEBUG_MODE)
        mined = self.nodes[0].generate(1)[0]
        epn = 1
        eph = self.nodes[0].generate(3)[-1]
        self.sync_all()

        # -------------------------- end epoch

        sc_funds_post = self.nodes[3].getscinfo(scid)['balance']
        assert_equal(sc_funds_post, sc_funds_pre - bt_amount)

        decoded_cert_post = self.nodes[2].getrawcertificate(cert, 1)
        assert_equal(decoded_cert_post['certid'], cert)
        assert_equal(decoded_cert_post['hex'], raw_cert)
        assert_equal(decoded_cert_post['blockhash'], mined)
        assert_equal(decoded_cert_post['confirmations'], 4)
        #remove fields not included in decoded_cert_pre_list
        del decoded_cert_post['hex']
        del decoded_cert_post['blockhash']
        del decoded_cert_post['confirmations']
        del decoded_cert_post['blocktime']
        decoded_cert_post_list = sorted(decoded_cert_post.items())

        mark_logs("check that cert decodes correctly", self.nodes, DEBUG_MODE)
        assert_equal(decoded_cert_pre_list, decoded_cert_post_list)

        mark_logs("check that SC balance has been decreased by the cert amount", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[2].getscinfo(scid)['balance'], (sc_amount - bt_amount))

        raw_addresses = {}
        raw_params = {"scid": scid, "endEpochBlockHash": eph, "fee": CERT_FEE, "withdrawalEpochNumber": epn}
        raw_cert = []
        cert = []

        # generate an empty certificate with only a fee
        try:
            raw_cert = self.nodes[0].createrawcertificate(raw_addresses, raw_params)
        except JSONRPCException, e:
            errorString = e.error['message']
            print "\n======> ", errorString
            assert_true(False)

        decoded_cert_pre = self.nodes[0].decoderawcertificate(raw_cert)
        decoded_cert_pre_list = sorted(decoded_cert_pre.items())

        mark_logs("Node0 sending raw certificate with no outputs for epoch {}".format(epn), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[0].sendrawcertificate(raw_cert)
        except JSONRPCException, e:
            errorString = e.error['message']
            print "\n======> ", errorString
            assert_true(False)

        self.sync_all()

        mark_logs("Node0 generating 1 block", self.nodes, DEBUG_MODE)
        mined = self.nodes[0].generate(1)[0]
        self.sync_all()

        # we enabled -txindex in zend therefore also node 2 can see it
        decoded_cert_post = self.nodes[2].getrawcertificate(cert, 1)

        mark_logs("check that cert contents are as expected", self.nodes, DEBUG_MODE)
        assert_equal(len(decoded_cert_post['vout']), 0)
        assert_equal(decoded_cert_post['certid'], cert)
        assert_equal(decoded_cert_post['hex'], raw_cert)
        assert_equal(decoded_cert_post['blockhash'], mined)
        assert_equal(decoded_cert_post['confirmations'], 1)
        assert_equal(Decimal(decoded_cert_post['cert']['totalAmount']), 0.0)
        #remove fields not included in decoded_cert_pre_list
        del decoded_cert_post['hex']
        del decoded_cert_post['blockhash']
        del decoded_cert_post['confirmations']
        del decoded_cert_post['blocktime']
        decoded_cert_post_list = sorted(decoded_cert_post.items())

        mark_logs("check that cert decodes correctly", self.nodes, DEBUG_MODE)
        assert_equal(decoded_cert_pre_list, decoded_cert_post_list)

        # check the miner got the cert fee in his coinbase
        coinbase = self.nodes[3].getblock(mined, True)['tx'][0]
        decoded_coinbase = self.nodes[2].getrawtransaction(coinbase, 1)
        miner_quota = decoded_coinbase['vout'][0]['value']
        mark_logs("check that the miner has got the cert fee", self.nodes, DEBUG_MODE)
        assert_equal(miner_quota, Decimal("7.5"))

        # check sc has the same balance as before this cert but the latest fee
        sc_funds_post_2 = self.nodes[3].getscinfo(scid)['balance']
        mark_logs("check that the Node 0 has been charged with the cert fee", self.nodes, DEBUG_MODE)
        assert_equal(sc_funds_post_2, sc_funds_post)


if __name__ == '__main__':
    sc_rawcert().main()
