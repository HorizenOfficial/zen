#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import MINIMAL_SC_HEIGHT, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, stop_nodes, get_epoch_data, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, mark_logs, \
    assert_false, assert_true, swap_bytes, \
    get_total_amount_from_listaddressgroupings
from test_framework.mc_test.mc_test import *
import os
import pprint
import time
from decimal import Decimal
from collections import namedtuple

DEBUG_MODE = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH = 5
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')


class sc_cert_maturity(BitcoinTestFramework):

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
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-scproofqueuesize=0', '-logtimemicros=1']] * NUMB_OF_NODES)

        for k in range(0, NUMB_OF_NODES-1):
            connect_nodes_bi(self.nodes, k, k+1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        ''' 
        ( 1) Create a SC 
        ( 2) Advance to next epoch
        ( 3) Send a cert to SC with 2 bwt to Node1 at the same address
        ( 4) checks some rpc cmd with bwt maturity info while the cert is in mempool 
        ( 5) Advance to next epoch and check Node1 can not spend bwt yet, and checks the rpc cmds with bwt maturity info
        ( 6) Send a second cert to SC with 1 bwt to Node1 at the same address
        ( 7) checks some rpc cmd with bwt maturity info while the cert is in mempool 
        ( 8) Restart nodes
        ( 9) check that Node1 still can not spend bwts also after reading persistent db
        (10) Node0 mines 2 more blocks and Node1 reach maturity of some of the bwts and checks again
             the rpc cmds with bwts maturity info
        '''

        creation_amount = Decimal("10.0")
        bwt_amount1      = Decimal("1.0")
        bwt_amount2      = Decimal("2.0")
        bwt_amount3      = Decimal("4.0")

        mark_logs("Node 0 generates {} block".format(MINIMAL_SC_HEIGHT), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(MINIMAL_SC_HEIGHT)
        self.sync_all()
        prev_epoch_hash = self.nodes[0].getbestblockhash()

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
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

        # Create a SC with a budget of 10 coins
        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs("Node 0 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        sc_creating_height = self.nodes[0].getblockcount()
        self.sync_all()

        mark_logs("Node0 generates 4 more blocks to achieve end of withdrawal epochs", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(4)
        self.sync_all()

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        bal_without_bwt = self.nodes[1].getbalance() 

        # node0 create a cert_1 for funding node1 
        addr_node1 = self.nodes[1].getnewaddress()
        amounts = [{"address": addr_node1, "amount": bwt_amount1}, {"address": addr_node1, "amount": bwt_amount2}]
        mark_logs("Node 0 sends a cert for scid {} with 2 bwd transfers of {} coins to Node1 address".format(scid, bwt_amount1+bwt_amount2, addr_node1), self.nodes, DEBUG_MODE)
        try:
            #Create proof for WCert
            quality = 1
            proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node1, addr_node1], [bwt_amount1, bwt_amount2])

            cert_1 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("==> certificate is {}".format(cert_1), self.nodes, DEBUG_MODE)
            self.sync_all()
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        # start first epoch + 2*epocs + safe guard
        bwtMaturityHeight = (sc_creating_height-1) + 2*EPOCH_LENGTH + 2

        # get the taddr of Node1 where the bwt is send to
        bwt_address = self.nodes[0].getrawtransaction(cert_1, 1)['vout'][1]['scriptPubKey']['addresses'][0]

        mark_logs("Check cert is in mempool", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_1 in self.nodes[1].getrawmempool())
        
        mark_logs("Check the output of the listtxesbyaddress cmd is as expected when cert is unconfirmed",
            self.nodes, DEBUG_MODE)
        res = self.nodes[1].listtxesbyaddress(bwt_address)
        assert_equal(res[0]['scid'], scid)
        assert_equal(res[0]['vout'][1]['maturityHeight'], bwtMaturityHeight)

        mark_logs("Check the there are immature outputs in the unconfirmed tx data when cert is unconfirmed", self.nodes, DEBUG_MODE)
        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        assert_equal(ud['bwtImmatureOutput'], Decimal("0.0")) #not bwt_amount1+bwt_amount2 because bwts in mempool are considered voided
        # unconf bwt do not contribute to unconfOutput
        assert_equal(ud['unconfirmedOutput'], Decimal("0.0"))

        mark_logs("Node0 mines cert and cert immature outputs appear the unconfirmed tx data", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        assert_equal(ud['bwtImmatureOutput'], bwt_amount1+bwt_amount2)
        assert_equal(ud['unconfirmedOutput'], Decimal("0.0"))

        mark_logs("Node0 generates 4 more blocks to achieve end of withdrawal epochs", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(4)
        self.sync_all()

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)


        # node0 create a cert_2 for funding node1 
        amounts = [{"address": addr_node1, "amount": bwt_amount3}]
        mark_logs("Node 0 sends a cert for scid {} with 1 bwd transfers of {} coins to Node1 address".format(scid, bwt_amount3, addr_node1), self.nodes, DEBUG_MODE)
        try:
            #Create proof for WCert
            quality = 1
            proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [addr_node1], [bwt_amount3])

            cert_2 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("==> certificate is {}".format(cert_2), self.nodes, DEBUG_MODE)
            self.sync_all()
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Check cert is in mempool", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_2 in self.nodes[1].getrawmempool())
        
        # get the taddr of Node1 where the bwt is send to
        bwt_address_new = self.nodes[0].getrawtransaction(cert_2, 1)['vout'][1]['scriptPubKey']['addresses'][0]
        assert_equal(bwt_address, bwt_address_new)

        mark_logs("Check the output of the listtxesbyaddress cmd is as expected",
            self.nodes, DEBUG_MODE)

        res = self.nodes[1].listtxesbyaddress(bwt_address)
        for entry in res:
            # same scid for both cert
            assert_equal(entry['scid'], scid)
            if entry['txid'] == cert_1:
                assert_equal(entry['vout'][1]['maturityHeight'], bwtMaturityHeight)
            if entry['txid'] == cert_2:
                assert_equal(entry['vout'][1]['maturityHeight'], bwtMaturityHeight+EPOCH_LENGTH)

        mark_logs("Check that unconfirmed certs bwts are not in the unconfirmed tx data", self.nodes, DEBUG_MODE)
        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        assert_equal(ud['bwtImmatureOutput'], bwt_amount1+bwt_amount2)

        mark_logs("Check Node1 has not bwt in its balance yet", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[1].getbalance(), bal_without_bwt) 
        assert_equal(self.nodes[1].z_getbalance(bwt_address), bal_without_bwt)

        lag_list = self.nodes[1].listaddressgroupings()
        assert_equal(get_total_amount_from_listaddressgroupings(lag_list), bal_without_bwt)

        mark_logs("Stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)
        self.sync_all()

        mark_logs("Check Node1 still has not bwt in its balance", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[1].getbalance(), bal_without_bwt) 
        assert_equal(self.nodes[1].z_getbalance(bwt_address), bal_without_bwt)

        mark_logs("Node0 generates 1 more block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Check Node1 still has not bwt in its balance also after 1 block is mined", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[1].getbalance(), bal_without_bwt) 
        assert_equal(self.nodes[1].z_getbalance(bwt_address), bal_without_bwt)

        mark_logs("Node0 generates 1 more block attaining the maturity of the first pair of bwts", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Check Node1 now has bwts in its balance, and their maturity height is as expected", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[1].getblockcount(), bwtMaturityHeight) 
        assert_equal(self.nodes[1].getbalance(), bwt_amount1+bwt_amount2) 
        assert_equal(self.nodes[1].z_getbalance(bwt_address), bwt_amount1+bwt_amount2)

        mark_logs("Check the output of the listtxesbyaddress cmd is not changed",
            self.nodes, DEBUG_MODE)
        res = self.nodes[1].listtxesbyaddress(bwt_address)
        for entry in res:
            # same scid for both cert
            assert_equal(entry['scid'], scid)
            if entry['txid'] == cert_1:
                assert_equal(entry['vout'][1]['maturityHeight'], bwtMaturityHeight)
            if entry['txid'] == cert_2:
                assert_equal(entry['vout'][1]['maturityHeight'], bwtMaturityHeight+EPOCH_LENGTH)

        mark_logs("Check the there are no immature outputs in the unconfirmed tx data but the last cert bwt", self.nodes, DEBUG_MODE)
        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        assert_equal(ud['bwtImmatureOutput'], bwt_amount3 )

        mark_logs("Node0 generates 4 more blocks approaching the ceasing limit height", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(4)
        self.sync_all()
        print "Height=", self.nodes[0].getblockcount()
        print "Ceasing at h =", self.nodes[0].getscinfo("*")['items'][0]['ceasingHeight']
        print "State =", self.nodes[0].getscinfo("*")['items'][0]['state']
        assert_equal(self.nodes[0].getscinfo("*")['items'][0]['state'], "ALIVE")

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        mark_logs("Node 0 sends an empty cert for scid {} just for keeping the sc alive".format(scid), self.nodes, DEBUG_MODE)
        try:
            #Create proof for WCert
            quality = 22
            proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [], [])

            cert_3 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, [], FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("==> certificate is {}".format(cert_3), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        self.sync_all()

        assert_equal(self.nodes[1].z_getbalance(bwt_address), bwt_amount1+bwt_amount2)
        assert_equal(self.nodes[1].getbalance(), bwt_amount1+bwt_amount2)
        assert_equal(self.nodes[1].listaccounts()[""], bwt_amount1+bwt_amount2)

        lag_list = self.nodes[1].listaddressgroupings()
        assert_equal(get_total_amount_from_listaddressgroupings(lag_list), bwt_amount1+bwt_amount2)
        assert_equal(Decimal(self.nodes[1].getreceivedbyaccount("")), bwt_amount1+bwt_amount2)

        mark_logs("Node0 generates 1 more block attaining the maturity of the last bwt", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        
        assert_equal(self.nodes[1].z_getbalance(bwt_address), bwt_amount1+bwt_amount2+bwt_amount3)
        assert_equal(self.nodes[1].getbalance(), bwt_amount1+bwt_amount2+bwt_amount3)
        assert_equal(self.nodes[1].listaccounts()[""], bwt_amount1+bwt_amount2+bwt_amount3)

        lag_list = self.nodes[1].listaddressgroupings()
        assert_equal(get_total_amount_from_listaddressgroupings(lag_list), bwt_amount1+bwt_amount2+bwt_amount3)
        assert_equal(Decimal(self.nodes[1].getreceivedbyaccount("")), bwt_amount1+bwt_amount2+bwt_amount3)

        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        assert_equal(ud['bwtImmatureOutput'], Decimal("0.0") )

        # lock the bwt utxo
        arr = [{"txid": cert_2, "vout":1}] 
        ret = self.nodes[1].lockunspent(False, arr)
        assert_equal(ret, True)
        self.sync_all()

        try:
            ret = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), Decimal("3.5")) 
            assert_true(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        # unlock the bwt utxo
        ret = self.nodes[1].lockunspent(True, arr)
        assert_equal(ret, True)
        self.sync_all()

        try:
            ret = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), Decimal("3.5")) 
            mark_logs("Send succeeded {}".format(ret), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert_true(False)



if __name__ == '__main__':
    sc_cert_maturity().main()
