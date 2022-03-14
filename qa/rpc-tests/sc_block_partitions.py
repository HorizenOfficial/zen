#!/usr/bin/env python2
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
CERT_FEE = Decimal("0.01")

BIT_VECTOR_BUF = "021f8b08000000000002ff017f0080ff44c7e21ba1c7c0a29de006cb8074e2ba39f15abfef2525a4cbb3f235734410bda21cdab6624de769ceec818ac6c2d3a01e382e357dce1f6e9a0ff281f0fedae0efe274351db37599af457984dcf8e3ae4479e0561341adfff4746fbe274d90f6f76b8a2552a6ebb98aee918c7ceac058f4c1ae0131249546ef5e22f4187a07da02ca5b7f000000"
BIT_VECTOR_FE  = "8a7d5229f440d4700d8b0343de4e14400d1cb87428abf83bd67153bf58871721"

TEST_BLOCK_TX_PARTITION_MAX_SIZE = 6000
TEST_BLOCK_MAX_SIZE = 3*TEST_BLOCK_TX_PARTITION_MAX_SIZE
TEST_BLOCK_PRIORITY_SIZE = 0

class sc_block_partitions(BitcoinTestFramework):
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
                                              '-blockmaxsize=%d'%TEST_BLOCK_MAX_SIZE,
                                              '-blocktxpartitionmaxsize=%d'%TEST_BLOCK_TX_PARTITION_MAX_SIZE,
                                              '-blockprioritysize=%d'%TEST_BLOCK_PRIORITY_SIZE,
                                              '-scproofqueuesize=0']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        Setup a node with a custom size for block size and tx partition size.
        Create a SC, advance epoch and create a bunch of certificates with progressive quality and high fee.
        Create also a number of txes with lower fee than certs and whose total size exceeds the tx partition size.
        - Check that the block is filled by high-fee certs and only the reminder of free space is taken up by txes
        - Check that only reserved block partition is occupied despite a greater block size
        '''


        def create_sc(cmdInput, node):
            try:
                res = node.sc_create(cmdInput)
                tx =   res['txid']
                scid = res['scid']
            except JSONRPCException, e:
                errorString = e.error['message']
                mark_logs(errorString,self.nodes,DEBUG_MODE)
                assert_true(False)

            return tx, scid

        # network topology: (0)--(1)

        print "Miners have max block size = {}, max tx partition size = {}".format(TEST_BLOCK_MAX_SIZE, TEST_BLOCK_TX_PARTITION_MAX_SIZE)

        mark_logs("Node 1 generates {} block".format(100), self.nodes, DEBUG_MODE)
        self.nodes[1].generate(100)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(MINIMAL_SC_HEIGHT-100), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(MINIMAL_SC_HEIGHT-100)
        self.sync_all()
        
        #generate Vks and constant
        certMcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)

        certVk = certMcTest.generate_params('scs')

        constant = generate_random_field_element_hex()

        amount = 1.0

        #-------------------------------------------------------
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': EPOCH_LENGTH,
            'amount': amount,
            'fee': 0.0001,
            'constant':constant ,
            'wCertVk': certVk,
            'toaddress':"cdcd",
        }
      
        tx, scid = create_sc(cmdInput, self.nodes[0])
        mark_logs("Created SC with scid={} via tx={}".format(scid, tx), self.nodes,DEBUG_MODE)
        self.sync_all()

        # advance epoch
        self.nodes[0].generate(EPOCH_LENGTH)
        self.sync_all()
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        mark_logs("Creating certs...", self.nodes, DEBUG_MODE)
        proofs = []
        q = 10
        tot_num_cert = 0
        tot_cert_sz = 0
        scid_swapped = str(swap_bytes(scid))

        while True:
            t0 = time.time()
            proof = certMcTest.create_test_proof(
                "scs", scid_swapped, epoch_number, (q+tot_num_cert), MBTR_SC_FEE, FT_SC_FEE,
                epoch_cum_tree_hash, constant, [], [], [])
            assert_true(proof != None)
            t1 = time.time()
            print "...proof generated: {} secs".format(t1-t0)
            proofs.append(proof)
        
            try:
                cert = self.nodes[0].sc_send_certificate(scid, epoch_number, (q+tot_num_cert),
                    epoch_cum_tree_hash, proof, [], FT_SC_FEE, MBTR_SC_FEE, CERT_FEE, "", [], [])
            except JSONRPCException, e:
                errorString = e.error['message']
                print "Send certificate failed with reason {}".format(errorString)
                assert(False)
            self.sync_all()
            tot_num_cert += 1
 
            #mark_logs("cert={}".format(cert), self.nodes, DEBUG_MODE)
            hexCert = self.nodes[0].getrawtransaction(cert)
            tot_cert_sz += len(hexCert)//2
            #print "sz=", len(hexCert)//2
            if tot_cert_sz > TEST_BLOCK_MAX_SIZE:
                break

        print "tot cert = {}, tot sz = {} ".format(tot_num_cert, tot_cert_sz)

        mark_logs("Creating txes...", self.nodes, DEBUG_MODE)
        tot_num_tx = 0
        tot_tx_sz = 0
        taddr_node1 = self.nodes[1].getnewaddress()

        # fee lower than the one used for certs
        fee = Decimal("0.000025")
        am = 0.1
        mc_return_address = self.nodes[1].getnewaddress()
        sc_ft = [ {"address":"abc", "amount":am, "scid":scid, "mcReturnAddress": mc_return_address} ]


        # there are 100 coinbase utxo now matured
        listunspent = self.nodes[1].listunspent()

        while True:
            utxo = listunspent[tot_num_tx]
            change = utxo['amount'] - Decimal(am) - Decimal(fee) 
            raw_inputs  = [ {'txid' : utxo['txid'], 'vout' : utxo['vout']}]
            raw_outs    = { taddr_node1: change }
            try:
                raw_tx = self.nodes[1].createrawtransaction(raw_inputs, raw_outs, [], [], sc_ft)
                signed_tx = self.nodes[1].signrawtransaction(raw_tx)
                tx = self.nodes[1].sendrawtransaction(signed_tx['hex'])
            except JSONRPCException, e:
                errorString = e.error['message']
                print "Send raw tx failed with reason {}".format(errorString)
                assert(False)

            tot_num_tx += 1
            hexTx = self.nodes[1].getrawtransaction(tx)
            tot_tx_sz += len(hexTx)//2
            if tot_tx_sz > 2*TEST_BLOCK_TX_PARTITION_MAX_SIZE:
                self.sync_all()
                break

        print "tot tx   = {}, tot sz = {} ".format(tot_num_tx, tot_tx_sz)

        mark_logs("Generating 1 block and checking blocks partitions...", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        ret = self.nodes[0].getmininginfo()

        print "block num of cert = ", ret['currentblockcert']
        print "block size        = ", ret['currentblocksize']
        print "block num of tx   = ", ret['currentblocktx']

        # all certs but one are included in the block
        assert_equal(ret['currentblockcert'], tot_num_cert - 1)
        # block space used for certs and txes is lower than max block size 
        assert_true(ret['currentblocksize'] < TEST_BLOCK_MAX_SIZE)

        mark_logs("Generating 1 block and checking blocks partitions...", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        ret = self.nodes[0].getmininginfo()

        print "block num of cert = ", ret['currentblockcert']
        print "block size        = ", ret['currentblocksize']
        print "block num of tx   = ", ret['currentblocktx']

        # last cert has been included in the block
        assert_equal(ret['currentblockcert'], 1)
        # block space used for certs and txes is lower than max block size 
        assert_true(ret['currentblocksize'] < TEST_BLOCK_MAX_SIZE)
        # block space used for txes is lower than tx partition
        assert_true(ret['currentblocktx'] > 0)

        mark_logs("Generating 1 block and checking blocks partitions...", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        ret = self.nodes[0].getmininginfo()

        print "block num of cert = ", ret['currentblockcert']
        print "block size        = ", ret['currentblocksize']
        print "block num of tx   = ", ret['currentblocktx']

        # no more certs
        assert_equal(ret['currentblockcert'], 0)
        # block space used for nd txes is lower than max block size 
        assert_true(ret['currentblocksize'] < TEST_BLOCK_MAX_SIZE)
        # block space used for txes is lower than tx partition
        assert_true(ret['currentblocktx'] > 0)

        self.nodes[0].generate(1)
        self.sync_all()
        ret = self.nodes[0].getmininginfo()

        # no more certs nor txes, it took 3 blocks as expected to mine all of them
        assert_equal(len(self.nodes[0].getrawmempool()), 0)
        assert_equal(ret['currentblockcert'], 0)
        assert_equal(ret['currentblocktx'], 0)

        
if __name__ == '__main__':
    sc_block_partitions().main()

