#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs,\
    get_epoch_data, \
    assert_false, assert_true
from test_framework.mc_test.mc_test import *
import os
import pprint
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 3
EPOCH_LENGTH = 5
CERT_FEE = Decimal('0.00015')
HIGH_CERT_FEE = Decimal('0.00015')
LOW_CERT_FEE = Decimal('0.00005')

class quality_blockchain(BitcoinTestFramework):

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
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib', '-logtimemicros=1']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        The test creates a sc, send funds to it and then sends a certificate to it,
        verifying also that specifying various combination of bad parameters causes a certificate
        to be refused. This test also checks that the receiver of cert backward transfer can spend it
        only when they become mature.
        '''

        # forward transfer amounts
        creation_amount = Decimal("0.5")
        fwt_amount = Decimal("200")
        bwt_amount_bad = Decimal("250.0")
        bwt_amount = Decimal("20")
        bwt_amount_2 = Decimal("30")

        self.nodes[0].getblockhash(0)

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates 220 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(220)
        self.sync_all()

        # SC creation
        bal_before_sc_creation = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance before SC creation: {}".format(bal_before_sc_creation), self.nodes, DEBUG_MODE)

        #generate wCertVk and constant
        mcTest = MCTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()

        ret = self.nodes[1].sc_create(EPOCH_LENGTH, "dada", creation_amount, vk, "", constant)
        creating_tx = ret['txid']
        scid = ret['scid']
        mark_logs("Node 1 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        sc_creating_height = self.nodes[0].getblockcount()
        self.sync_all()

        # Check node 1 balance following sc creation
        fee_sc_creation = self.nodes[1].gettransaction(creating_tx)['fee']
        mark_logs("Fee paid for SC creation: {}".format(fee_sc_creation), self.nodes, DEBUG_MODE)
        bal_after_sc_creation = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance after SC creation: {}".format(bal_after_sc_creation), self.nodes, DEBUG_MODE)
        assert_equal(bal_before_sc_creation, bal_after_sc_creation + creation_amount - fee_sc_creation)

        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], Decimal(0))
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['immature amounts'][0]['amount'], creation_amount)

        # Fwd Transfer to Sc
        bal_before_fwd_tx = self.nodes[0].getbalance("", 0)
        mark_logs("Node0 balance before fwd tx: {}".format(bal_before_fwd_tx), self.nodes, DEBUG_MODE)
        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount, scid)
        mark_logs("Node0 transfers {} coins to SC with tx {}...".format(fwt_amount, fwd_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node0 confirms fwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # Check node 0 balance following fwd tx
        fee_fwt = self.nodes[0].gettransaction(fwd_tx)['fee']
        mark_logs("Fee paid for fwd tx: {}".format(fee_fwt), self.nodes, DEBUG_MODE)
        bal_after_fwd_tx = self.nodes[0].getbalance("", 0)
        mark_logs("Node0 balance after fwd: {}".format(bal_after_fwd_tx), self.nodes, DEBUG_MODE)
        assert_equal(bal_before_fwd_tx, bal_after_fwd_tx + fwt_amount - fee_fwt - Decimal(8.75))  # 8.75 is matured coinbase

        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], Decimal(0))
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['immature amounts'][0]['amount'], creation_amount)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['immature amounts'][1]['amount'], fwt_amount)

        mark_logs("Node0 generating 3 more blocks to achieve end of withdrawal epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(3)
        self.sync_all()
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc balance has matured
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immature amounts']), 0)

        epoch_block_hash, epoch_number = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_block_hash = {}".format(epoch_number, epoch_block_hash), self.nodes, DEBUG_MODE)

        prev_epoch_block_hash = self.nodes[0].getblockhash(sc_creating_height - 1 + ((epoch_number) * EPOCH_LENGTH))

        pkh_node1 = self.nodes[1].getnewaddress("", True)

        #Create proof for WCert
        quality = 0
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality, constant, [pkh_node1], [bwt_amount])

        mark_logs("Node 0 tries to perform a bwd transfer with insufficient Sc balance...", self.nodes, DEBUG_MODE)
        amounts = [{"pubkeyhash": pkh_node1, "amount": bwt_amount_bad}]

        try:
            self.nodes[0].send_certificate(scid, epoch_number, quality, epoch_block_hash, proof, amounts, CERT_FEE)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        mark_logs(errorString, self.nodes, DEBUG_MODE)
        #assert_equal(True, "sidechain has insufficient funds" in errorString)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount)
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immature amounts']), 0)

        mark_logs("Node 0 tries to perform a bwd transfer with an invalid epoch number ...", self.nodes, DEBUG_MODE)
        amount_cert_1 = [{"pubkeyhash": pkh_node1, "amount": bwt_amount}]

        try:
            self.nodes[0].send_certificate(scid, epoch_number + 1, quality, epoch_block_hash, proof, amount_cert_1, CERT_FEE)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("invalid epoch data" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immature amounts']), 0)

        mark_logs("Node 0 tries to perform a bwd transfer with an invalid quality ...", self.nodes, DEBUG_MODE)

        try:
            self.nodes[0].send_certificate(scid, epoch_number, quality - 1, epoch_block_hash, proof, amount_cert_1, CERT_FEE)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("Invalid quality parameter" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immature amounts']), 0)

        #--------------------------------------------------------------------------------------
        mark_logs("Node 0 tries to perform a bwd transfer using a wrong vk for the scProof...", self.nodes, DEBUG_MODE)

        # let's generate new params and create a correct proof with them 
        mcTest.generate_params("sc_temp")

        quality = 100

        wrong_proof = mcTest.create_test_proof(
            "sc_temp", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality, constant, [pkh_node1], [bwt_amount])

        try:
            self.nodes[0].send_certificate(scid, epoch_number, quality, epoch_block_hash, wrong_proof, amount_cert_1, CERT_FEE)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("bad-sc-cert-not-applicable" in errorString, True)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc has not been affected by faulty certificate
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immature amounts']), 0)

        #---------------------end scProof tests-------------------------
        amount_cert = [{"pubkeyhash": pkh_node1, "amount": bwt_amount}]

        # Create Cert1 with quality 100 and place it in mempool
        mark_logs("Create Cert1 with quality 100 and place it in mempool", self.nodes, DEBUG_MODE)
        quality = 100
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality, constant, [pkh_node1], [bwt_amount])
        try:
            cert_1_epoch_0 = self.nodes[0].send_certificate(scid, epoch_number, quality, epoch_block_hash, proof, amount_cert, CERT_FEE)
            assert(len(cert_1_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_1_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Checking mempools alignement", self.nodes, DEBUG_MODE)
        self.sync_all()
        for i in range(1, NUMB_OF_NODES):
            assert_equal(sorted(self.nodes[0].getrawmempool()), sorted(self.nodes[i].getrawmempool()))

        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_1_epoch_0 in self.nodes[0].getrawmempool())

        # Create Cert2 with lower quality and place it in mempool
        mark_logs("# Create Cert2 with quality 80 and place it in mempool", self.nodes, DEBUG_MODE)
        low_quality_proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality - 20, constant, [pkh_node1], [bwt_amount])
        try:
            cert_2_epoch_0 = self.nodes[0].send_certificate(scid, epoch_number, quality - 20, epoch_block_hash, low_quality_proof, amount_cert, HIGH_CERT_FEE)
            assert_equal(True, cert_1_epoch_0 in self.nodes[0].getrawmempool())
            assert(len(cert_2_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_2_epoch_0), self.nodes, DEBUG_MODE)
            assert_equal(True, cert_2_epoch_0 in self.nodes[0].getrawmempool())
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        # Checking all certificate in mempool
        assert_equal(True, cert_1_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(True, cert_2_epoch_0 in self.nodes[0].getrawmempool())

        mark_logs("Node0 confims bwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)[0]
        self.sync_all()

        # Checking all certificate in mempool
        mark_logs("Check cert is not in mempool anymore", self.nodes, DEBUG_MODE)
        assert_equal(False, cert_1_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(False, cert_2_epoch_0 in self.nodes[0].getrawmempool())

        # Create Cert1 with quality 100 and place it in mempool
        mark_logs("Create Cert3 with quality 90 and try to place it in mempool", self.nodes, DEBUG_MODE)
        quality = 90
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality, constant, [pkh_node1], [bwt_amount])
        try:
            cert_3_epoch_0 = self.nodes[0].send_certificate(scid, epoch_number, quality, epoch_block_hash, proof, amount_cert, CERT_FEE)
            assert(len(cert_3_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_3_epoch_0), self.nodes, DEBUG_MODE)
            assert_equal(False, cert_3_epoch_0 in self.nodes[0].getrawmempool())
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        mark_logs("Create Cert3 with quality 100 and try to place it in mempool", self.nodes, DEBUG_MODE)
        quality = 100
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality, constant, [pkh_node1], [bwt_amount])
        try:
            cert_3_epoch_0 = self.nodes[0].send_certificate(scid, epoch_number, quality, epoch_block_hash, proof, amount_cert, CERT_FEE)
            assert(len(cert_3_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_3_epoch_0), self.nodes, DEBUG_MODE)
            assert_equal(False, cert_3_epoch_0 in self.nodes[0].getrawmempool())
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        mark_logs("Create Cert3 with quality 100 and try to place it in mempool", self.nodes, DEBUG_MODE)
        quality = 100
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality, constant, [pkh_node1], [bwt_amount])
        try:
            cert_3_epoch_0 = self.nodes[0].send_certificate(scid, epoch_number, quality, epoch_block_hash, proof, amount_cert, HIGH_CERT_FEE)
            assert(len(cert_3_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_3_epoch_0), self.nodes, DEBUG_MODE)
            assert_equal(False, cert_3_epoch_0 in self.nodes[0].getrawmempool())
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        mark_logs("Create Cert3 with quality 120 and try to place it in mempool", self.nodes, DEBUG_MODE)
        quality = 120
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality, constant, [pkh_node1], [bwt_amount])
        try:
            cert_3_epoch_0 = self.nodes[0].send_certificate(scid, epoch_number, quality, epoch_block_hash, proof, amount_cert, CERT_FEE)
            assert(len(cert_3_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_3_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        # Checking all certificate in mempool
        assert_equal(True, cert_3_epoch_0 in self.nodes[0].getrawmempool())

        mark_logs("Create Cert4 with quality 110 and try to place it in mempool", self.nodes, DEBUG_MODE)
        quality = 110
        amount_cert_4 = [{"pubkeyhash": pkh_node1, "amount": bwt_amount_2}]
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality, constant, [pkh_node1], [bwt_amount_2])
        try:
            cert_4_epoch_0 = self.nodes[0].send_certificate(scid, epoch_number, quality, epoch_block_hash, proof,
                                                            amount_cert_4, CERT_FEE)
            assert (len(cert_4_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_4_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        # Checking all certificate in mempool
        assert_equal(True, cert_3_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(True, cert_4_epoch_0 in self.nodes[0].getrawmempool())

        mark_logs("Create Cert5 with quality 100 and try to place it in mempool", self.nodes, DEBUG_MODE)
        quality = 100
        amount_cert_5 = [{"pubkeyhash": pkh_node1, "amount": bwt_amount}]
        proof = mcTest.create_test_proof(
            "sc1", epoch_number, epoch_block_hash, prev_epoch_block_hash,
            quality, constant, [pkh_node1], [bwt_amount])
        try:
            cert_5_epoch_0 = self.nodes[0].send_certificate(scid, epoch_number, quality, epoch_block_hash, proof, amount_cert_5, CERT_FEE)
            assert(len(cert_5_epoch_0) > 0)
            mark_logs("Certificate is {}".format(cert_5_epoch_0), self.nodes, DEBUG_MODE)
            assert_equal(False, cert_5_epoch_0 in self.nodes[0].getrawmempool())
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        mark_logs("Node0 confims bwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)[0]
        self.sync_all()

        # Checking all certificate in mempool
        mark_logs("Check cert is not in mempool anymore", self.nodes, DEBUG_MODE)
        assert_equal(False, cert_1_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(False, cert_2_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(False, cert_3_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(False, cert_4_epoch_0 in self.nodes[0].getrawmempool())

if __name__ == '__main__':
    quality_blockchain().main()
