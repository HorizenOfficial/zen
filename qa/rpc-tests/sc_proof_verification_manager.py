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
WAIT_ON_FAIL_PROOFS = 1
WAIT_ON_PASS_PROOFS = 2
WAIT_ON_FAIL_PASS_PROOFS = 3

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
        self.nodes += [start_node(0, self.options.tmpdir,extra_args=[f'-scproofverificationdelay={BATCH_VERIFICATION_MAX_DELAY_ARG}', '-logtimemicros'])]
        self.nodes += [start_node(1, self.options.tmpdir,extra_args=[f'-scproofverificationdelay={BATCH_VERIFICATION_MAX_DELAY_ARG}', '-logtimemicros'])]

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
        assert_equal(node1_stats["pendingCerts"] + node1_stats["pendingCSWs"], 0)
        assert_equal(node1_stats["failedCerts"] + node1_stats["failedCSWs"] +
                     node1_stats["okCerts"] + node1_stats["okCSWs"], 0)

        print("Test is OK")


    def wait_for_proofs(self, node, wait_on_type, quantity = 1):
        for i in range(6000): # one minute at worst (0.01 * 6000 = 60)
            node_stats = node.getproofverifierstats()
            if (wait_on_type == WAIT_ON_PENDING_PROOFS):
                if (node_stats["pendingCerts"] + node_stats["pendingCSWs"] == quantity):
                    print(f"{quantity} proofs scheduled for verification on node async proof verifier")
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
