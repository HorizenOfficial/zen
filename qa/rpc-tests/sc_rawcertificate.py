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
from decimal import *
import operator
import pprint
from random import randrange

import time

DEBUG_MODE = 1
EPOCH_LENGTH = 5
NUMB_OF_NODES = 4
CERT_FEE = Decimal("0.000135")

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

        mark_logs("Node 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs("Node 3 generates 219 block", self.nodes, DEBUG_MODE)
        self.nodes[3].generate(219)
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

        mark_logs("Node3 generating 5 block", self.nodes, DEBUG_MODE)
        epn = 0
        eph = self.nodes[3].generate(EPOCH_LENGTH)[-1]
        self.sync_all()

        # -------------------------- end epoch

        sc_funds_pre = self.nodes[3].getscinfo(scid)['balance']

        pkh_node2 = self.nodes[2].getnewaddress("", True)

        mark_logs("Node3 generating 2 block, overcoming safeguard", self.nodes, DEBUG_MODE)
        self.nodes[3].generate(2)
        self.sync_all()

        raw_inputs   = []
        raw_outs     = {}
        raw_bwt_outs = {pkh_node2: bt_amount}
        raw_params = {"scid": scid, "endEpochBlockHash": eph, "withdrawalEpochNumber": epn}
        raw_cert = []
        cert = []

        try:
            raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            # sign will not do anything useful since we specified no inputs
            signed_cert = self.nodes[1].signrawcertificate(raw_cert)
            assert_equal(raw_cert, signed_cert['hex'])
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

        node0_bal_before = self.nodes[0].getbalance()

        raw_inputs   = []
        raw_outs     = {}
        raw_bwt_outs = {}
        raw_params = {"scid": scid, "endEpochBlockHash": eph, "withdrawalEpochNumber": epn}
        raw_cert = []
        cert = []

        # get a UTXO for setting fee
        utx = False
        listunspent = self.nodes[0].listunspent()
        for aUtx in listunspent:
            if aUtx['amount'] > CERT_FEE:
                utx = aUtx
                change = aUtx['amount'] - CERT_FEE
                break;

        assert_equal(utx!=False, True)

        raw_inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        raw_outs    = { self.nodes[0].getnewaddress() : change }

        # generate a certificate with no backward transfers and only a fee
        try:
            raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[0].signrawcertificate(raw_cert)
        except JSONRPCException, e:
            errorString = e.error['message']
            print "\n======> ", errorString
            assert_true(False)

        decoded_cert_pre = self.nodes[0].decoderawcertificate(signed_cert['hex'])
        decoded_cert_pre_list = sorted(decoded_cert_pre.items())

        mark_logs("Node3 sending raw certificate with no backward transfer for epoch {}".format(epn), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[3].sendrawcertificate(signed_cert['hex'])
        except JSONRPCException, e:
            errorString = e.error['message']
            print "\n======> ", errorString
            assert_true(False)

        self.sync_all()

        mark_logs("Node2 generating 1 block", self.nodes, DEBUG_MODE)
        mined = self.nodes[2].generate(1)[0]
        self.sync_all()

        # we enabled -txindex in zend therefore also node 2 can see it
        decoded_cert_post = self.nodes[2].getrawcertificate(cert, 1)

        mark_logs("check that cert contents are as expected", self.nodes, DEBUG_MODE)
        # vout contains just the change 
        assert_equal(len(decoded_cert_post['vout']), 1)
        assert_equal(decoded_cert_post['certid'], cert)
        assert_equal(decoded_cert_post['hex'], signed_cert['hex'])
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
        assert_equal(miner_quota, Decimal("7.5") + CERT_FEE)

        # check that the Node 0 has been charged with the cert fee
        node0_bal_after = self.nodes[0].getbalance()
        mark_logs("check that the Node 0, the creator of the cert, which have been actully sent by Node3, has been charged with the fee", self.nodes, DEBUG_MODE)
        assert_equal(node0_bal_after, node0_bal_before - CERT_FEE)

        mark_logs("Node0 generating 4 block reaching next epoch", self.nodes, DEBUG_MODE)
        eph = self.nodes[0].generate(4)[-1]
        epn = 2
        self.sync_all()

        # -------------------------- end epoch

        raw_inputs   = []
        raw_outs     = {}
        raw_bwt_outs = {}
        raw_params = {"scid": scid, "endEpochBlockHash": eph, "withdrawalEpochNumber": epn}
        raw_cert = []
        cert = []

        # get some UTXO for handling many vin 
        totalAmount = Decimal("100.0")
        totalUtxoAmount = Decimal("0")
        listunspent = self.nodes[3].listunspent()
        for aUtx in listunspent:
            if totalUtxoAmount < totalAmount :
                utx = aUtx
                raw_inputs.append({'txid' : utx['txid'], 'vout' : utx['vout']})
                totalUtxoAmount += utx['amount']
            else:
                break

        assert_true(totalUtxoAmount >= totalAmount)

        change = totalUtxoAmount - CERT_FEE

        numbOfChunks = 50
        chunkValueBt  = Decimal(sc_funds_post/numbOfChunks) 
        chunkValueOut = Decimal(change/numbOfChunks) 

        for k in range(0, numbOfChunks):
            pkh_node1 = self.nodes[1].getnewaddress("", True)
            raw_bwt_outs.update({pkh_node1:chunkValueBt})
            taddr = self.nodes[3].getnewaddress()
            raw_outs.update({ taddr : chunkValueOut })

        totBwtOuts = len(raw_bwt_outs)*chunkValueBt
        totOuts    = len(raw_outs)*chunkValueOut
        certFee    = totalUtxoAmount - Decimal(totOuts)

        # generate a certificate with some backward transfer, several vin vout and a fee
        try:
            raw_cert    = self.nodes[3].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[3].signrawcertificate(raw_cert)
            # let a different node, Node0, send it
            mark_logs("Node1 sending raw certificate for epoch {}".format(epn), self.nodes, DEBUG_MODE)
            cert        = self.nodes[1].sendrawcertificate(signed_cert['hex'])
        except JSONRPCException, e:
            errorString = e.error['message']
            print "\n======> ", errorString
            assert_true(False)

        self.sync_all()
        decoded_cert_post = self.nodes[0].getrawcertificate(cert, 1)

        mark_logs("check that cert contents are as expected", self.nodes, DEBUG_MODE)
        assert_equal(decoded_cert_post['certid'], cert)
        # vin contains the expected numb of utxo
        assert_equal(len(decoded_cert_post['vin']), len(raw_inputs))
        # vout contains the change and the backward transfers 
        assert_equal(len(decoded_cert_post['vout']),  len(raw_outs) + len(raw_bwt_outs))
        assert_equal(decoded_cert_post['hex'], signed_cert['hex'])
        assert_equal(Decimal(decoded_cert_post['cert']['totalAmount']), Decimal(totBwtOuts))
        assert_equal(self.nodes[3].gettransaction(cert)['fee'], -certFee)

        mark_logs("Node0 generating 5 block reaching next epoch", self.nodes, DEBUG_MODE)
        eph = self.nodes[0].generate(5)[-1]
        epn = 3
        self.sync_all()
        
        # get a UTXO for setting fee
        utx = False
        listunspent = self.nodes[0].listunspent()
        for aUtx in listunspent:
            if aUtx['amount'] > CERT_FEE:
                utx = aUtx
                change = aUtx['amount'] - CERT_FEE
                break;

        assert_equal(utx!=False, True)

        raw_inputs   = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        raw_outs     = { self.nodes[0].getnewaddress() : change }
        raw_bwt_outs = {}
        raw_params   = {"scid": scid, "endEpochBlockHash": eph, "withdrawalEpochNumber": epn}
        raw_cert     = []
        pk_arr       = []

        # generate a certificate which is expected to fail to be signed by passing a wrong private key
        pk_arr = []
        pk_bad = self.nodes[1].dumpprivkey(self.nodes[1].getnewaddress() )
        pk_arr.append(pk_bad)

        try:
            mark_logs("Node0 creates and signs a raw certificate for epoch {}, expecting failure because the priv key is not his...".format(epn), self.nodes, DEBUG_MODE)
            raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[0].signrawcertificate(raw_cert, pk_arr)
            assert_equal(signed_cert['complete'], False)
        except JSONRPCException, e:
            errorString = e.error['message']
            print "\n======> ", errorString
            assert_true(False)

        # retry adding the right key
        pk_good = self.nodes[0].dumpprivkey(utx['address'])
        pk_arr.append(pk_good)

        try:
            mark_logs("Node0 creates and signs a raw certificate for epoch {}, expecting success because the priv key is the right one...".format(epn), self.nodes, DEBUG_MODE)
            raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[0].signrawcertificate(raw_cert, pk_arr)
            assert_equal(signed_cert['complete'], True)
        except JSONRPCException, e:
            errorString = e.error['message']
            print "\n======> ", errorString

        mark_logs("Node2 sending raw certificate for epoch {}".format(epn), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[2].sendrawcertificate(signed_cert['hex'])
        except JSONRPCException, e:
            errorString = e.error['message']
            print "\n======> ", errorString
            assert_true(False)

        self.sync_all()




if __name__ == '__main__':
    sc_rawcert().main()
