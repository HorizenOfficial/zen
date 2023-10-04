#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_node, connect_nodes, get_epoch_data, \
    swap_bytes, sync_blocks, sync_mempools, assert_greater_than
from test_framework.mc_test.mc_test import CertTestUtils, generate_random_field_element_hex

from decimal import Decimal
import time

EPOCH_LENGTH = 100
CERT_FEE = Decimal("0.000123")
SC_FEE = Decimal("0.000345")
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
MINIMAL_SC_HEIGHT = 420

BATCH_VERIFICATION_MAX_DELAY_ARG = 20000
BATCH_VERIFICATION_PROCESSING_ESTIMATION = 2000

# in order to have slower execution of proof verification
PROOVING_SYSTEM_SLOW = "darlin"
CERT_NUM_CONSTRAINTS_SLOW = 1 << 20
SEGMENT_SIZE_SLOW = 1 << 18

# in order to have faster execution of proof verification
PROOVING_SYSTEM_FAST = "cob_marlin"
CERT_NUM_CONSTRAINTS_FAST = 1 << 10
SEGMENT_SIZE_FAST = 1 << 11

# type of wait
WAIT_ON_PENDING_PROOFS = 0
WAIT_ON_IN_VERIFICATION_PROOFS = 1
WAIT_ON_FAIL_PROOFS = 2
WAIT_ON_PASS_PROOFS = 3
WAIT_ON_FAIL_PASS_PROOFS = 4

class sc_proof_verification_manager(BitcoinTestFramework):
    FEE = 0.0001
    FT_SC_FEE = Decimal('0')
    MBTR_SC_FEE = Decimal('0')
    CERT_FEE = Decimal('0.00015')

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 2)

    def setup_network(self, split=False):
        self.nodes=[]
        self.nodes += [start_node(0, self.options.tmpdir,extra_args=[f'-scproofverificationdelay={BATCH_VERIFICATION_MAX_DELAY_ARG}', '-logtimemicros', '-debug=sc', '-debug=bench', '-debug=cert'])]
        self.nodes += [start_node(1, self.options.tmpdir,extra_args=[f'-scproofverificationdelay={BATCH_VERIFICATION_MAX_DELAY_ARG}', '-logtimemicros', '-debug=sc', '-debug=bench', '-debug=cert'])]

        connect_nodes(self.nodes, 0, 1) # non-bidirectional connection is set in order to avoid receiving twice invs (especially when clearing mempool)

        self.is_network_split=False
        self.sync_all()

    def run_test(self):

        # amounts
        creation_amount = Decimal("50")
        bwt_amount = Decimal("5")
        tAddr1 = self.nodes[1].getnewaddress()
        node1Addr = self.nodes[1].validateaddress(tAddr1)['address']
        self.nodes[0].generate(MINIMAL_SC_HEIGHT)
        self.sync_all()
        
        ########### Create a sidechain ##################
        print("########### Create a sidechain ##################")
        
        print(f"Using {PROOVING_SYSTEM_SLOW} proving system")
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir, PROOVING_SYSTEM_SLOW)
        vk = mcTest.generate_params("sc1", 'cert', num_constraints=CERT_NUM_CONSTRAINTS_SLOW, segment_size=SEGMENT_SIZE_SLOW)
        constant = generate_random_field_element_hex()

        cmdInput = {
            "version": 0,
            'withdrawalEpochLength': EPOCH_LENGTH,
            'toaddress': "dada",
            'amount': creation_amount,
            'wCertVk': vk,
            'constant': constant
        }

        ret = self.nodes[0].sc_create(cmdInput)
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        self.sync_all()

        # Mine block creating the sidechain
        self.nodes[0].generate(1)
        self.sync_all()

        # Advance for 1 Epoch
        self.nodes[0].generate(EPOCH_LENGTH)
        self.sync_all()


        # ---------- TEST ----------
        # a certificate is sent by node0 and the mempools are cleared when the proof has been scheduled for verification on
        # node1 but has still not started being verified; even after waiting for the time that would be required for the
        # certificate to start being verified and then to complete being verified, the mempools are empty

        self.send_certificate_from_node(self.nodes[0], "sc1", scid, 1, mcTest, scid_swapped, constant, node1Addr, bwt_amount, CERT_NUM_CONSTRAINTS_SLOW, SEGMENT_SIZE_SLOW)

        print("Checking mempools before clearing")
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 1)
        # node1 has received the certificate, but it has been queued in async proof verifier
        assert_equal(self.nodes[1].getmempoolinfo()["size"], 0)

        print("Mempools clearing")
        self.nodes[0].clearmempool()
        # clear mempools with proper timing on node1
        self.wait_for_proofs(self.nodes[1], WAIT_ON_PENDING_PROOFS, 1)
        self.nodes[1].clearmempool()

        print("Checking mempools after clearing")
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 0)
        assert_equal(self.nodes[1].getmempoolinfo()["size"], 0)

        print("Waiting for the time the certificate (node1) would have required to start being verified and then to complete being verified...")
        time.sleep((BATCH_VERIFICATION_MAX_DELAY_ARG + BATCH_VERIFICATION_PROCESSING_ESTIMATION) / 1000)

        print("Checking mempools and stats after waiting enough time")
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 0)
        assert_equal(self.nodes[1].getmempoolinfo()["size"], 0)
        node1_stats = self.nodes[1].getproofverifierstats()
        assert_equal(node1_stats["pendingCerts"] + node1_stats["pendingCSWs"] +
                     node1_stats["pendingCertsInVerification"] + node1_stats["pendingCSWsInVerification"], 0)
        assert_equal(node1_stats["failedCerts"] + node1_stats["failedCSWs"] +
                     node1_stats["okCerts"] + node1_stats["okCSWs"], 0)
        assert_equal(node1_stats["removedFromQueueProofs"], 1)

        print("Test is OK")


        # ---------- TEST ----------
        # a certificate is sent by node0 and the mempools are cleared when the proof has started being verified on node1; even
        # after waiting for the time that would be required for the certificate to complete being verified, the mempools are empty

        self.send_certificate_from_node(self.nodes[0], "sc1", scid, 2, mcTest, scid_swapped, constant, node1Addr, bwt_amount, CERT_NUM_CONSTRAINTS_SLOW, SEGMENT_SIZE_SLOW)

        print("Checking mempools before clearing")
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 1)
        # node1 has received the certificate, but it has been queued in async proof verifier
        assert_equal(self.nodes[1].getmempoolinfo()["size"], 0)

        print("Mempools clearing")
        self.nodes[0].clearmempool()
        # clear mempools with proper timing on node1
        self.wait_for_proofs(self.nodes[1], WAIT_ON_IN_VERIFICATION_PROOFS, 1)
        self.nodes[1].clearmempool()

        print("Checking mempools after clearing")
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 0)
        assert_equal(self.nodes[1].getmempoolinfo()["size"], 0)

        print("Waiting for the time the certificate (node1) would have required to complete being verified...")
        time.sleep((BATCH_VERIFICATION_PROCESSING_ESTIMATION) / 1000)

        print("Checking mempools and stats after waiting enough time")
        assert_equal(self.nodes[0].getmempoolinfo()["size"], 0)
        assert_equal(self.nodes[1].getmempoolinfo()["size"], 0)
        node1_stats = self.nodes[1].getproofverifierstats()
        assert_equal(node1_stats["pendingCerts"] + node1_stats["pendingCSWs"] +
                     node1_stats["pendingCertsInVerification"] + node1_stats["pendingCSWsInVerification"], 0)
        assert_equal(node1_stats["failedCerts"] + node1_stats["failedCSWs"] +
                     node1_stats["okCerts"] + node1_stats["okCSWs"], 0)
        assert_equal(node1_stats["discardedResultsProofs"], 1)

        print("Test is OK")


        # ---------- TEST ----------
        # a certificate is sent by node0, after the proof has been scheduled for verification on node1 (but before it has
        # started being verified) a block is mined by node0; even after waiting for the time that would be required for the
        # certificate to start being verified and then to complete being verified, the statistics for async proof verifier
        # report no certificate has been verified, thus showing no useless (the result being already available) proof
        # verification has been performed

        self.send_certificate_from_node(self.nodes[0], "sc1", scid, 3, mcTest, scid_swapped, constant, node1Addr, bwt_amount, CERT_NUM_CONSTRAINTS_SLOW, SEGMENT_SIZE_SLOW)

        # mine a block with proper timing on node1
        self.wait_for_proofs(self.nodes[1], WAIT_ON_PENDING_PROOFS, 1)
        self.nodes[0].generate(1)
        sync_blocks(self.nodes[:2])

        print("Waiting for the certificate (node1) to start being verified and then to complete being verified...")
        time.sleep((BATCH_VERIFICATION_MAX_DELAY_ARG + BATCH_VERIFICATION_PROCESSING_ESTIMATION) / 1000)

        print("Checking mempools and stats after waiting enough time")
        # the stats for node1 are expected to be all null because the certificate has been synchronously verified
        node1_stats = self.nodes[1].getproofverifierstats()
        assert_equal(node1_stats["pendingCerts"] + node1_stats["pendingCSWs"] +
                     node1_stats["pendingCertsInVerification"] + node1_stats["pendingCSWsInVerification"], 0)
        assert_equal(node1_stats["failedCerts"] + node1_stats["failedCSWs"] +
                     node1_stats["okCerts"] + node1_stats["okCSWs"], 0)
        assert_equal(node1_stats["removedFromQueueProofs"], 1 + 1)

        print("Test is OK")


        # ---------- TEST ----------
        # a certificate is sent by node0 and a block is mined by node0, then a synchronization of blockchains on node0 and
        # on node1 is performed, measuring the time of mining and synchronizing; a certificate is sent by node0 and a
        # synchronization only of the mempools on node0 and node1 is performed (hence allowing for certificate validation),
        # then a block is mined by node0, then a synchronization of mempools and blockchains on node0 and node1 is performed,
        # measuring the time of mining and synchronizing; this second measurement must be (much) shorter than the first one

        self.send_certificate_from_node(self.nodes[0], "sc1", scid, 4, mcTest, scid_swapped, constant, node1Addr, bwt_amount, CERT_NUM_CONSTRAINTS_SLOW, SEGMENT_SIZE_SLOW)

        # mine a block that would require proof verification (on node1) and sync
        timeBeforeBlock = time.time()
        self.nodes[0].generate(1)
        sync_blocks(self.nodes[0:2], wait=0.1)
        timeAfterBlock = time.time()
        elapsedTimeWithProofVerification = timeAfterBlock - timeBeforeBlock
        print(f"Elapsed time mining a block (containing a certificate) with proof verification: {elapsedTimeWithProofVerification}")
        self.sync_all() 

        self.send_certificate_from_node(self.nodes[0], "sc1", scid, 5, mcTest, scid_swapped, constant, node1Addr, bwt_amount, CERT_NUM_CONSTRAINTS_SLOW, SEGMENT_SIZE_SLOW)
        sync_mempools(self.nodes)

        # mine a block that would not require proof verification (on neither node) and sync
        timeBeforeBlock = time.time()
        self.nodes[0].generate(1)
        sync_blocks(self.nodes[0:2], wait=0.1)
        timeAfterBlock = time.time()
        elapsedTimeWithoutProofVerification = timeAfterBlock - timeBeforeBlock
        print(f"Elapsed time mining a block (containing a certificate) without proof verification: {elapsedTimeWithoutProofVerification}")
        self.sync_all()

        print(f"Checking elapsed time with proof verification is greater than elapsed time without proof verification")
        assert_greater_than(elapsedTimeWithProofVerification, elapsedTimeWithoutProofVerification)

        print("Test is OK")


        # ---------- TEST ----------
        # a certificate is sent by node0, a block is mined by node0, then some time elapses and a second certificate is sent
        # by node0; a check is perfomed in order to attest the async verification has been triggered due to second certificate
        # queue age (not first certificate queue age, whose result is already available)
        #
        # timeline from node1 perspective:
        #
        # 0--------------------------------->t
        #
        # 1--------------------?............ (cert1 enters async queue)
        # ..B............................... (block arrives and is checked)
        # ..1V.............................. (cert1 is sync verified)
        # ...1X............................. (cert1 is removed from async queue)
        # .................................. (some time waiting)
        # ...........2--------------------?. (cert2 enters async queue)
        # ................................2V (cert2 enters verification)
        #                      ^          ^
        #                      |__________|

        ########### Create a sidechain ##################
        print("########### Create a sidechain ##################")
        
        print(f"Using {PROOVING_SYSTEM_FAST} proving system")
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir, PROOVING_SYSTEM_FAST)
        vk = mcTest.generate_params("sc2", 'cert', num_constraints=CERT_NUM_CONSTRAINTS_FAST, segment_size=SEGMENT_SIZE_FAST)

        constant = generate_random_field_element_hex()

        cmdInput = {
            "version": 0,
            'withdrawalEpochLength': EPOCH_LENGTH,
            'toaddress': "dada",
            'amount': creation_amount,
            'wCertVk': vk,
            'constant': constant
        }

        ret = self.nodes[0].sc_create(cmdInput)
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        self.sync_all()

        # Mine block creating the sidechain
        self.nodes[0].generate(1)
        self.sync_all()

        # Advance for 1 Epoch
        self.nodes[0].generate(EPOCH_LENGTH)
        self.sync_all()

        self.send_certificate_from_node(self.nodes[0], "sc2", scid, 1, mcTest, scid_swapped, constant, node1Addr, bwt_amount, CERT_NUM_CONSTRAINTS_FAST, SEGMENT_SIZE_FAST)

        # mine a block (with proper timing on node1)
        self.wait_for_proofs(self.nodes[1], WAIT_ON_PENDING_PROOFS, 1)
        timeFirstCertificateEnteredQueue = time.time()
        print(f"Time first certificate entered queue (node1): {timeFirstCertificateEnteredQueue}")

        timeFirstCertificateWouldHaveStartedVerification = timeFirstCertificateEnteredQueue + (BATCH_VERIFICATION_MAX_DELAY_ARG) / 1000
        print(f"Time first certificate would have started verification (node1): {timeFirstCertificateWouldHaveStartedVerification}")

        self.nodes[0].generate(1)
        sync_blocks(self.nodes[:2])
        self.wait_for_proofs(self.nodes[1], WAIT_ON_PENDING_PROOFS, 0)

        # in order to make the difference more significant
        print("Waiting for the certificate (node1) to arrive almost at half of queue time...")
        time.sleep((BATCH_VERIFICATION_MAX_DELAY_ARG) / 1000 / 2 - (time.time() - timeFirstCertificateEnteredQueue))
        print(f"Finished waiting: {time.time()}")

        self.send_certificate_from_node(self.nodes[0], "sc2", scid, 2, mcTest, scid_swapped, constant, node1Addr, bwt_amount, CERT_NUM_CONSTRAINTS_FAST, SEGMENT_SIZE_FAST)
        self.wait_for_proofs(self.nodes[1], WAIT_ON_PENDING_PROOFS, 1)
        self.wait_for_proofs(self.nodes[1], WAIT_ON_PENDING_PROOFS, 0)
        timeSecondCertificateStartedVerification = time.time()
        print(f"Time second certificate started verification: {timeSecondCertificateStartedVerification}")

        print(f"Checking time second certificate started verification is much greater than time first certificate would have started verification")
        assert_greater_than(timeSecondCertificateStartedVerification, timeFirstCertificateWouldHaveStartedVerification + (BATCH_VERIFICATION_MAX_DELAY_ARG) / 1000 / 2)

        print("Test is OK")


    def wait_for_proofs(self, node, wait_on_type, quantity = 1):
        for i in range(6000): # one minute at worst (0.01 * 6000 = 60)
            node_stats = node.getproofverifierstats()
            if (wait_on_type == WAIT_ON_PENDING_PROOFS):
                if (node_stats["pendingCerts"] + node_stats["pendingCSWs"] == quantity):
                    print(f"{quantity} proofs scheduled for verification on node async proof verifier")
                    break
            elif (wait_on_type == WAIT_ON_IN_VERIFICATION_PROOFS):
                if (node_stats["pendingCertsInVerification"] + node_stats["pendingCSWsInVerification"] == quantity):
                    print(f"{quantity} proofs in verification on node async proof verifier")
                    break
            elif (wait_on_type == WAIT_ON_FAIL_PROOFS):
                if (node_stats["failedCerts"] + node_stats["failedCSWs"] == quantity):
                    print(f"{quantity} proofs verified (fail) on node async proof verifier")
                    break
            elif (wait_on_type == WAIT_ON_PASS_PROOFS):
                if (node_stats["okCerts"] + node_stats["okCSWs"] == quantity):
                    print(f"{quantity} proofs verified (pass) on node async proof verifier")
                    break
            elif (wait_on_type == WAIT_ON_FAIL_PASS_PROOFS):
                if (node_stats["failedCerts"] + node_stats["failedCSWs"] + node_stats["okCerts"] + node_stats["okCSWs"] == quantity):
                    print(f"{quantity} proofs verified (fail or pass) on node async proof verifier")
                    break
            time.sleep(0.01) # hundredth of a second
    
    def send_certificate_from_node(self, node, id, scid, certquality, mcTest, scid_swapped, constant, node1Addr, bwt_amount, cert_num_constraints, segment_size):
        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        quality = certquality
        proof = mcTest.create_test_proof(id, scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, None, constant, [node1Addr], [bwt_amount],
                                         [], cert_num_constraints, segment_size)
        amount_cert = [{"address": node1Addr, "amount": bwt_amount}]
        print(f"########### Send a certificate with quality = {certquality} ##################")
        node.sc_send_certificate(scid, epoch_number, quality,
                                 epoch_cum_tree_hash, proof, amount_cert, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)


if __name__ == '__main__':
    sc_proof_verification_manager().main()