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
    dump_sc_info, dump_sc_info_record, get_epoch_data, get_spendable, swap_bytes, advance_epoch, \
    get_field_element_with_padding
from test_framework.mc_test.mc_test import *
import os
import pprint
from decimal import Decimal
import json
import time

NUMB_OF_NODES = 2
DEBUG_MODE = 1
EPOCH_LENGTH = 20
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal("0.000123")

BIT_VECTOR_BUF = "021f8b08000000000002ff017f0080ff44c7e21ba1c7c0a29de006cb8074e2ba39f15abfef2525a4cbb3f235734410bda21cdab6624de769ceec818ac6c2d3a01e382e357dce1f6e9a0ff281f0fedae0efe274351db37599af457984dcf8e3ae4479e0561341adfff4746fbe274d90f6f76b8a2552a6ebb98aee918c7ceac058f4c1ae0131249546ef5e22f4187a07da02ca5b7f000000"
BIT_VECTOR_FE  = "8a7d5229f440d4700d8b0343de4e14400d1cb87428abf83bd67153bf58871721"

class sc_big_block(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[['-logtimemicros=1', '-debug=py', '-debug=net',
                                              #'-debug=sc', '-debug=mempool', '-debug=cert', '-par=5',
                                              '-scproofqueuesize=0', '-debug=bench']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        Create a few SCs having the same parameters, advance 2 epochs and then let half of them cease.
        For the SCs alive send a certificate, for the ceased ones send a csw, then mine a block, which 
        should contain all of cert and csw with related proofs.
        Restart the network and check DB integrity.
        '''
#================================================================================
# Modify these params for customizing the test:
# -------------------------------------------------------------------------------
# the number of sidechain to be used in the test (min 2)
        #TOT_NUM_OF_SIDECHAINS = 100
        TOT_NUM_OF_SIDECHAINS = 2
# parameters for tuning the complexity (and the size) of the created proofs
# These have impacts both in execution times and also on disk space
# TO TEST: 
#      SEGMENT_SIZE = 1 << 17
#   1) CERT_NUM_CONSTRAINTS = 1 << 19, CSW_NUM_CONSTRAINTS = 1 << 18;
#   2) CERT_NUM_CONSTRAINTS = 1 << 20, CSW_NUM_CONSTRAINTS = 1 << 19;
        CERT_NUM_CONSTRAINTS = 1 << 13
        CSW_NUM_CONSTRAINTS = 1 << 13

#        CERT_PROVING_SYSTEM = "darlin"
#        CSW_PROVING_SYSTEM = "darlin"
        CERT_PROVING_SYSTEM = "cob_marlin"
        CSW_PROVING_SYSTEM = "cob_marlin"

# Segment size should be  at most 2 powers less than number of constraints, otherwise out-of-size vk could be produced
        SEGMENT_SIZE = 1 << 11
#================================================================================
        assert_true(TOT_NUM_OF_SIDECHAINS >=2)


        def create_sc(cmdInput, node):
            try:
                res = node.sc_create(cmdInput)
                tx =   res['txid']
                scid = res['scid']
            except JSONRPCException, e:
                errorString = e.error['message']
                mark_logs(errorString,self.nodes,DEBUG_MODE)
                assert_true(False);

            return tx, scid

        def advance_sidechains_epoch(num_of_scs):

            for i in range(0, num_of_scs):

                if i == 0:
                    self.nodes[0].generate(EPOCH_LENGTH)
                    self.sync_all()
                    # these parameters are valid for all scs since they share the same epoch length 
                    epoch_number, epoch_cum_tree_hash = get_epoch_data(scids[i], self.nodes[0], EPOCH_LENGTH)

                print "Generating cert proof..."
                t0 = time.time()
                scid_swapped = str(swap_bytes(scids[i]))

                proof = certMcTest.create_test_proof(
                    "scs", scid_swapped, epoch_number, q, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [], [], proofCfeArray,
                    CERT_NUM_CONSTRAINTS, SEGMENT_SIZE)
                assert_true(proof != None)
                t1 = time.time()
                print "...proof generated: {} secs".format(t1-t0)
 
                try:
                    cert = self.nodes[0].sc_send_certificate(scids[i], epoch_number, q,
                        epoch_cum_tree_hash, proof, [], FT_SC_FEE, MBTR_SC_FEE, CERT_FEE, "", vCfe, vCmt)
                except JSONRPCException, e:
                    errorString = e.error['message']
                    print "Send certificate failed with reason {}".format(errorString)
                    assert(False)
                self.sync_all()
 
                mark_logs("==> certificate for SC{} epoch {} {} (chain height={})".format(i, epoch_number, cert, self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
                #pprint.pprint(self.nodes[0].getrawmempool())

        # network topology: (0)--(1)

        mark_logs("Node 1 generates 2 block",self.nodes,DEBUG_MODE)
        self.nodes[1].generate(2)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(MINIMAL_SC_HEIGHT), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(MINIMAL_SC_HEIGHT)
        self.sync_all()

        #generate Vks and constant
        certMcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir, CERT_PROVING_SYSTEM)
        cswMcTest = CSWTestUtils(self.options.tmpdir, self.options.srcdir, CSW_PROVING_SYSTEM)

        certVk = certMcTest.generate_params('scs', 'cert', CERT_NUM_CONSTRAINTS, SEGMENT_SIZE)
        cswVk  = cswMcTest.generate_params('scs', 'csw', CSW_NUM_CONSTRAINTS, SEGMENT_SIZE)

        constant = generate_random_field_element_hex()

        amount = 1.0

        #-------------------------------------------------------
        fee = 0.000025
        feCfg = []
        cmtCfg = []

        # all certs must have custom FieldElements with exactly those values as size in bits 
        feCfg.append([31])

        # one custom bv element with:
        # - as many bits in the uncompressed form (must be divisible by 254 and 8)
        # - up to 151 bytes in the compressed form
        cmtCfg.append([[254*4, 151]])

        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': EPOCH_LENGTH,
            'amount': amount,
            'fee': fee,
            'constant':constant ,
            'wCertVk': certVk,
            'wCeasedVk': cswVk,
            'toaddress':"cdcd",
            'vFieldElementCertificateFieldConfig':feCfg[0],
            'vBitVectorCertificateFieldConfig':cmtCfg[0]
        }
      
        scids = []
        scc_txs = []

        for i in range(0, TOT_NUM_OF_SIDECHAINS):
            tx, scid = create_sc(cmdInput, self.nodes[0]);
            mark_logs("Created SC {} with scid={} via tx={}".format(i, scid, tx), self.nodes,DEBUG_MODE)
            scids.append(scid)
            scc_txs.append(tx)
            self.sync_all()

        vCfe = ["ab000100"]
        vCmt = [BIT_VECTOR_BUF]

        fe1 = get_field_element_with_padding("ab000100", 0)
        fe2 = BIT_VECTOR_FE

        proofCfeArray = [fe1, fe2]
        q=10

        for j in range(0, 2):
            mark_logs("Let 1 epochs pass by and generate all certificates...", self.nodes, DEBUG_MODE)
            advance_sidechains_epoch(TOT_NUM_OF_SIDECHAINS)

        self.nodes[0].generate(1)

        self.sync_all()

        for j in range(0, 2):
            mark_logs("Let half of the SCs cease...", self.nodes, DEBUG_MODE)
            advance_sidechains_epoch(TOT_NUM_OF_SIDECHAINS/2)
  

        # check SCs status
        for k in range(0, TOT_NUM_OF_SIDECHAINS):
            sc_info = self.nodes[1].getscinfo(scids[k])['items'][0]
            print "SC{} - {}".format(k, sc_info["state"])

     
        for scid in scids[TOT_NUM_OF_SIDECHAINS/2:]:
            sc_csw_amount = Decimal("0.1")
 
            # CSW sender MC address, in taddress and pub key hash formats
            csw_mc_address = self.nodes[0].getnewaddress()
            actCertData            = self.nodes[0].getactivecertdatahash(scid)['certDataHash']
            ceasingCumScTxCommTree = self.nodes[0].getceasingcumsccommtreehash(scid)['ceasingCumScTxCommTree']
 
            nullifier = generate_random_field_element_hex()
            scid_swapped = swap_bytes(scid)
 
            print "Generating csw proof..."
            t0 = time.time()
            sc_proof = cswMcTest.create_test_proof(
                "scs", sc_csw_amount, str(scid_swapped), nullifier, csw_mc_address, ceasingCumScTxCommTree,
                actCertData, constant, CSW_NUM_CONSTRAINTS, SEGMENT_SIZE)
            assert_true(sc_proof != None)
            t1 = time.time()
            print "...proof generated: {} secs".format(t1-t0)
 
            sc_csws = [
            {
                "amount": sc_csw_amount,
                "senderAddress": csw_mc_address,
                "scId": scid,
                "epoch": 0,
                "nullifier": nullifier,
                "activeCertData": actCertData,
                "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
                "scProof": sc_proof
            }]
        
            
            # recipient MC address
            taddr = self.nodes[1].getnewaddress()
            sc_csw_tx_outs = {taddr: (Decimal(sc_csw_amount) + Decimal("0.15"))}
 
            rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws)
            funded_tx = self.nodes[0].fundrawtransaction(rawtx)
            sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
            finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
            mark_logs("sent csw tx {}".format(finalRawtx), self.nodes, DEBUG_MODE)

            self.sync_all()
            #pprint.pprint(self.nodes[0].getrawmempool())
            #pprint.pprint(self.nodes[0].getrawtransaction(finalRawtx, 1))
            #print

        mark_logs("Generating big block...", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        pprint.pprint(self.nodes[0].getblock(str(self.nodes[0].getblockcount())))
        pprint.pprint(self.nodes[0].getrawmempool())

        mark_logs("Checking persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        
if __name__ == '__main__':
    sc_big_block().main()

