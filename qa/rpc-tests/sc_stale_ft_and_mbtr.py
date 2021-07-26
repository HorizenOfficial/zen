#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_true, assert_false, initialize_chain_clean, \
    stop_nodes, wait_bitcoinds, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, disconnect_nodes, mark_logs, \
    dump_sc_info_record, get_epoch_data, swap_bytes, advance_epoch
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal
import pprint
import time

NUMB_OF_NODES = 3
DEBUG_MODE = 1
SC_COINS_MAT = 2
EPOCH_LENGTH = 10
MAX_MONEY = 21000000
CERT_FEE = Decimal('0.015') # high fee rate
BLK_MAX_SZ = 5000
NUM_BLOCK_FOR_SC_FEE_CHECK = EPOCH_LENGTH+1

class SCStaleFtAndMbtrTest(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[["-sccoinsmaturity=%d" % SC_COINS_MAT, '-scproofqueuesize=0', '-logtimemicros=1', '-debug=sc',
                                              '-debug=py', '-debug=mempool', '-debug=net',
                                              '-blockprioritysize=100',
                                              '-blockmaxsize=%d'%BLK_MAX_SZ,
                                              '-sc_num_blocks_for_scfee_check=%d'%NUM_BLOCK_FOR_SC_FEE_CHECK,
                                              '-debug=bench']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        This test checks that sidechain Forward Transfer fee, Mainchain Backward Transfer Request fee,
        and Mainchain Backward Transfer Request data length are set and handled correctly.
        '''

        # network topology: (0)--(1)--(2)

        mark_logs("Node 0 generates 310 blocks", self.nodes, DEBUG_MODE)
        blocks = self.nodes[0].generate(310)
        self.sync_all()

        mark_logs("Node 1 generates 110 blocks", self.nodes, DEBUG_MODE)
        blocks = self.nodes[1].generate(110)
        self.sync_all()

        # Sidechain parameters
        withdrawalEpochLength = EPOCH_LENGTH
        address = "dada"
        creation_amount = Decimal("5.0")
        custom_data = ""
        mbtrRequestDataLength = 1

        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk_tag = "sc1"
        vk = mcTest.generate_params(vk_tag)
        constant = generate_random_field_element_hex()
        cswVk  = ""
        feCfg = []
        bvCfg = []


        # ---------------------------------------------------------------------------------------
        # Node 1 - Create a valid sidechain
        mark_logs("\nNode 1 creates a new sidechain", self.nodes, DEBUG_MODE)

        errorString = ""
        ftScFee = 0.00022
        mbtrScFee = 0.00033

        try:
            ret = self.nodes[1].sc_create(withdrawalEpochLength, address, creation_amount, vk,
                custom_data, constant, cswVk, feCfg, bvCfg, ftScFee, mbtrScFee, mbtrRequestDataLength)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        creating_tx = ret['txid']
        scid = ret['scid']
        mark_logs("tx={}, scFee={}, scid={}".format(creating_tx, ftScFee, scid), self.nodes, DEBUG_MODE)
        self.sync_all()
        #pprint.pprint(self.nodes[0].getrawmempool(True))

        # send a very small amount to node 2, which will use it as input
        # for the FT, which will have then very low priority
        mark_logs("Node 1 send a small coin to Node2", self.nodes, DEBUG_MODE)
        taddr2 = self.nodes[2].getnewaddress()
        tx_for_input = self.nodes[1].sendtoaddress(taddr2, 0.001)
        mark_logs("tx={}".format(tx_for_input), self.nodes, DEBUG_MODE)
        self.sync_all()

        # ---------------------------------------------------------------------------------------
        mark_logs("Node 1 generates " + str(EPOCH_LENGTH) + " blocks", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[1].generate(EPOCH_LENGTH))
        self.sync_all()

        errorString = ""
        ftScFee = 0.00023
        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 2 creates a tx with a FT output", self.nodes, DEBUG_MODE)

        forwardTransferOuts = [{'toaddress': address, 'amount': ftScFee, "scid":scid}]

        # beside having a low priority (see above) it also is free: very low probability to get mined 
        # if the block size is small and there are other txes
        try:
            txFT = self.nodes[2].send_to_sidechain(forwardTransferOuts, { "fee": 0.0})
        except JSONRPCException, e:
            errorString = e.error['message']
            print(errorString)
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()
        mark_logs("txFT={}, scFee={}".format(txFT, ftScFee), self.nodes, DEBUG_MODE)
        #pprint.pprint(self.nodes[1].getrawmempool(True)[txFT])

        mark_logs("\nNode 0 creates a new certificate updating FT fees", self.nodes, DEBUG_MODE)

        quality = 1
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[1], EPOCH_LENGTH)
        pkh_node1 = self.nodes[1].getnewaddress("", True)
        cert_amount = Decimal("1.0")
        amount_cert_1 = [{"pubkeyhash": pkh_node1, "amount": cert_amount}]

        ftScFee = 0.00024
        scid_swapped = str(swap_bytes(scid))

        proof = mcTest.create_test_proof(
            vk_tag, scid_swapped, epoch_number, quality, mbtrScFee, ftScFee, epoch_cum_tree_hash,
            constant, [pkh_node1], [cert_amount])

        #raw_input("__________")
        cert = self.nodes[0].send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_1, ftScFee, mbtrScFee, CERT_FEE)

        self.sync_all()

        #pprint.pprint(self.nodes[1].getrawmempool(True))
        mark_logs("cert={}, epoch={}, ftScFee={}, q={}".format(cert, epoch_number, ftScFee, quality), self.nodes, DEBUG_MODE)

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        bl = self.nodes[1].generate(1)[-1]
        self.sync_all()
        
        #pprint.pprint(self.nodes[1].getrawmempool(True))
        mark_logs("Check cert is in block just mined...", self.nodes, DEBUG_MODE)
        assert_true(cert in self.nodes[0].getblock(bl, True)['cert'])
        mark_logs("Check tx is not in block just mined...", self.nodes, DEBUG_MODE)
        assert_false(txFT in self.nodes[0].getblock(bl, True)['tx'])
        assert_true(txFT in self.nodes[0].getrawmempool(True))

        mark_logs("\nNode 0 creates a second certificate of higher quality updating FT fees", self.nodes, DEBUG_MODE)

        quality += 1
        ftScFee = 0.0004
        proof = mcTest.create_test_proof(
            vk_tag, scid_swapped, epoch_number, quality, mbtrScFee, ftScFee, epoch_cum_tree_hash,
            constant, [pkh_node1], [cert_amount])

        cert = self.nodes[0].send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_1, ftScFee, mbtrScFee, CERT_FEE)

        self.sync_all()

        #pprint.pprint(self.nodes[1].getrawmempool(True))
        #pprint.pprint(self.nodes[0].getscinfo(scid))
        mark_logs("cert={}, epoch={}, ftScFee={}, q={}".format(cert, epoch_number, ftScFee, quality), self.nodes, DEBUG_MODE)

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        bl = self.nodes[1].generate(1)[-1]
        self.sync_all()
        
        mark_logs("Check cert is in block just mined...", self.nodes, DEBUG_MODE)
        assert_true(cert in self.nodes[0].getblock(bl, True)['cert'])
        mark_logs("Check tx is not in block just mined...", self.nodes, DEBUG_MODE)
        assert_false(txFT in self.nodes[0].getblock(bl, True)['tx'])
        assert_true(txFT in self.nodes[0].getrawmempool(True))

        #------------------------------------------------------------------------------------------------        
        # flood the mempool with non-free and hi-prio txes so that next blocks (with small max size) will
        # not include FT, which is free and with a low priority
        mark_logs("Creating many txes...", self.nodes, DEBUG_MODE)
        tot_num_tx = 0
        tot_tx_sz = 0
        taddr_node1 = self.nodes[0].getnewaddress()

        fee = Decimal('0.001')

        # there are a few coinbase utxo now matured
        listunspent = self.nodes[0].listunspent()
        print "num of utxo: ", len(listunspent)

        while True:
            if len(listunspent) <= tot_num_tx:
                # all utxo have been spent
                self.sync_all()
                break

            utxo = listunspent[tot_num_tx]
            change = utxo['amount'] - Decimal(fee) 
            raw_inputs  = [ {'txid' : utxo['txid'], 'vout' : utxo['vout']}]
            raw_outs    = { taddr_node1: change }
            try:
                raw_tx = self.nodes[0].createrawtransaction(raw_inputs, raw_outs)
                signed_tx = self.nodes[0].signrawtransaction(raw_tx)
                tx = self.nodes[0].sendrawtransaction(signed_tx['hex'])
            except JSONRPCException, e:
                errorString = e.error['message']
                print "Send raw tx failed with reason {}".format(errorString)
                assert(False)

            tot_num_tx += 1
            hexTx = self.nodes[0].getrawtransaction(tx)
            sz = len(hexTx)//2
            tot_tx_sz += sz
            #print("tx={}, sz={}, zatPerK={}".format(tx, sz, round((fee*COIN*1000))/sz))

            if tot_tx_sz > 5*EPOCH_LENGTH*BLK_MAX_SZ:
                self.sync_all()
                break

        print "tot tx   = {}, tot sz = {} ".format(tot_num_tx, tot_tx_sz)

        mpr = self.nodes[1].getrawmempool()
        print "num of tx in mempool: ", len(mpr)

        # advance two epochs
        mark_logs("\nLet 2 epochs pass by...", self.nodes, DEBUG_MODE)

        mark_logs("Node 1 generates " + str(EPOCH_LENGTH-2) + " blocks", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(EPOCH_LENGTH-2)

        mpr = self.nodes[1].getrawmempool()
        print "num of tx in mempool: ", len(mpr)

        ftScFee = 0.0008
        cert, epoch_number = advance_epoch(
            mcTest, self.nodes[1], self.sync_all, scid, "sc1", constant, EPOCH_LENGTH,
            quality, CERT_FEE, ftScFee, mbtrScFee, generate=False)

        mpr = self.nodes[1].getrawmempool()
        print "num of tx in mempool: ", len(mpr)
        #raw_input("__________")

        mark_logs("cert={}, epoch={}, ftScFee={}, q={}".format(cert, epoch_number, ftScFee, quality), self.nodes, DEBUG_MODE)
        assert_true(txFT in self.nodes[1].getrawmempool(True))

        ftScFee = 0.0010
        cert, epoch_number = advance_epoch(
            mcTest, self.nodes[1], self.sync_all, scid, "sc1", constant, EPOCH_LENGTH,
            quality, CERT_FEE, ftScFee, mbtrScFee)

        mpr = self.nodes[1].getrawmempool()
        print "num of tx in mempool: ", len(mpr)

        mark_logs("cert={}, epoch={}, ftScFee={}, q={}".format(cert, epoch_number, ftScFee, quality), self.nodes, DEBUG_MODE)
        assert_true(txFT in self.nodes[1].getrawmempool(True))

        mpr = self.nodes[1].getrawmempool()
        print "num of tx in mempool: ", len(mpr)

        #pprint.pprint(self.nodes[1].getrawmempool(True))

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        bl = self.nodes[1].generate(1)[-1]
        self.sync_all()

        assert_false(txFT in self.nodes[1].getrawmempool(True))
        assert_false(txFT in self.nodes[0].getblock(bl, True)['tx'])


if __name__ == '__main__':
    SCStaleFtAndMbtrTest().main()
