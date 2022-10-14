#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, mark_logs
from test_framework.blockchainhelper import BlockchainHelper

DEBUG_MODE = 1
NUMB_OF_NODES = 1

class sc_getcertmaturityinfo(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-txindex', '-scproofqueuesize=0', '-logtimemicros=1']] * NUMB_OF_NODES)

        self.is_network_split = split
        self.sync_all()

    def check_certificate_maturity_info(self, certificate_hash: str, blocks_to_maturity: int, certificate_state: str, maturity_height: int):
        '''
        Check that the certificate maturity info for the given certificate hash have the expected values
        (blocksToMaturity, certificateState, and maturityHeight).
        '''
        maturity_info: dict = self.nodes[0].getcertmaturityinfo(certificate_hash)
        assert_equal(maturity_info["blocksToMaturity"], blocks_to_maturity)
        assert_equal(maturity_info["certificateState"], certificate_state)
        assert_equal(maturity_info["maturityHeight"], maturity_height)

    def run_test(self):

        ''' 
        This test is intended to check that the RPC command getcertmaturityinfo works properly.
        '''

        # Reach the non-ceasing sidechains fork point
        test_helper = BlockchainHelper(self)
        mark_logs(f"Node 0 generates {ForkHeights['NON_CEASING_SC']} blocks to reach the non-ceasing sidechains fork", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['NON_CEASING_SC'])

        mark_logs("Node 0 creates a v1 sidechain", self.nodes, DEBUG_MODE)
        v1_sc1_name: str = "v1_sc1"
        test_helper.create_sidechain(v1_sc1_name, 1)

        mark_logs("Node 0 creates a v2 ceasing sidechain", self.nodes, DEBUG_MODE)
        v2_ceasing_sc2_name: str = "v2_ceasing_sc2"
        test_helper.create_sidechain(v2_ceasing_sc2_name, 2)

        mark_logs("Node 0 creates a v2 non-ceasing sidechain", self.nodes, DEBUG_MODE)
        v2_non_ceasing_sc3_name: str = "v2_non_ceasing_sc3"
        test_helper.create_sidechain(v2_non_ceasing_sc3_name, 2, { "withdrawalEpochLength": 0, "wCeasedVk": "" })

        # Now we have 3 sidechains in mempool:
        # - sc1 with version 1 and normal ceasing behavior
        # - sc2 with version 2 and normal ceasing behavior
        # - sc3 with version 2 and non-ceasing behavior

        mark_logs("Node 0 generates a block to confirm the creation of the 3 sidechains", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)

        epoch_length: int = test_helper.sidechain_map[v1_sc1_name]["creation_args"].withdrawalEpochLength
        submission_window_length: int = epoch_length // 10
        assert_equal(epoch_length, test_helper.sidechain_map[v2_ceasing_sc2_name]["creation_args"].withdrawalEpochLength)

        mark_logs(f"Node 0 generates {epoch_length - 1} blocks to reach end of epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(epoch_length - 1)

        mark_logs("Node 0 creates a certificate for the 3 sidechains", self.nodes, DEBUG_MODE)
        cert1_sc1: str = test_helper.send_certificate(v1_sc1_name, 10) # Epoch 0
        cert1_sc2: str = test_helper.send_certificate(v2_ceasing_sc2_name, 10) # Epoch 0
        cert1_sc3: str = test_helper.send_certificate(v2_non_ceasing_sc3_name, 0, self.nodes[0].getblockcount()-1) # Epoch 0, reference one block in the past

        mark_logs("Node 0 generates another certificate for each sidechain", self.nodes, DEBUG_MODE)
        cert2_sc1: str = test_helper.send_certificate(v1_sc1_name, 20) # Higher quality, same epoch 0
        cert2_sc2: str = test_helper.send_certificate(v2_ceasing_sc2_name, 20) # Higher quality, same epoch 0
        cert2_sc3: str = test_helper.send_certificate(v2_non_ceasing_sc3_name, 0) # Same 0 quality, epoch 1

        mark_logs("Check maturity info for the low quality certificates in the mempool (they should be not confirmed)", self.nodes, DEBUG_MODE)
        for cert in [cert1_sc1, cert1_sc2]:
            self.check_certificate_maturity_info(cert, -1, "LOW_QUALITY_MEMPOOL", -1)

        for cert in [cert1_sc3, cert2_sc1, cert2_sc2, cert2_sc3]:
            self.check_certificate_maturity_info(cert, -1, "TOP_QUALITY_MEMPOOL", -1)

        mark_logs("Node 0 generates a block to confirm the creation of the certificates", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)

        # Now we have the following situation for the 3 sidechains:
        # - sc1 -> two certificates, one superseded, the other immature (epoch 0)
        # - sc2 -> two certificates, one superseded, the other immature (epoch 0)
        # - sc3 -> two certificates, one for epoch 0 and one for epoch 1, both mature

        certificate_including_block_height = self.nodes[0].getblockcount()
        blocks_to_maturity: int = epoch_length + submission_window_length

        # Certificates for sc1 and sc2 (both with ceasing behavior) must stay "IMMATURE" until we reach
        # the end of the submission window of the next epoch. Certificate for sc3 (non-ceasing behavior),
        # on the other side, should be "MATURE" immediately.
        mark_logs("Check maturity info for the 3 certificates included in a block", self.nodes, DEBUG_MODE)
        for _ in range(blocks_to_maturity):
            current_block: int = self.nodes[0].getblockcount()

            # Check superseded certificates (low quality)
            for cert in [cert1_sc1, cert1_sc2]:
                # When a certificate is superseded, blocksToMaturity is set to -1 and the maturityHeight to the opposite value
                self.check_certificate_maturity_info(cert, -1, "SUPERSEDED", (current_block + blocks_to_maturity) * (-1))

            # Check immature certificates (top quality)
            for cert in [cert2_sc1, cert2_sc2]:
                self.check_certificate_maturity_info(cert, blocks_to_maturity, "IMMATURE", current_block + blocks_to_maturity)

            # Check mature certificates (for non-ceasing sidechain)
            for cert in [cert1_sc3, cert2_sc3]:
                self.check_certificate_maturity_info(cert, 0, "MATURE", certificate_including_block_height)

            self.nodes[0].generate(1)
            blocks_to_maturity -= 1

        # Having mined the last block of the submission window for sc1 and sc2, certificates must now be "SUPERSEDED".
        current_block: int = self.nodes[0].getblockcount()
        mark_logs("Check that the first two sidechains are ceased and all their certificates are SUPERSEDED", self.nodes, DEBUG_MODE)
        for cert in [cert1_sc1, cert1_sc2, cert2_sc1, cert2_sc2]:
            # When a certificate is superseded, blocksToMaturity is set to -1 and the maturityHeight to the opposite
            # of the height at which he would have become mature if the sidechain didn't cease.
            self.check_certificate_maturity_info(cert, -1, "SUPERSEDED", (current_block + blocks_to_maturity) * (-1))

        mark_logs("Check that the non-ceasing sidechain (sc3) certificates are still MATURE", self.nodes, DEBUG_MODE)
        for cert in [cert1_sc3, cert2_sc3]:
            self.check_certificate_maturity_info(cert, 0, "MATURE", certificate_including_block_height)

        mark_logs("Revert all blocks until all certificates are sent back to mempool", self.nodes, DEBUG_MODE)
        for _ in range(current_block - certificate_including_block_height):
            reverting_block_hash = self.nodes[0].getbestblockhash()
            self.nodes[0].invalidateblock(reverting_block_hash)
            current_block: int = self.nodes[0].getblockcount()
            blocks_to_maturity += 1

            # Check superseded certificates (low quality)
            for cert in [cert1_sc1, cert1_sc2]:
                # When a certificate is superseded, blocksToMaturity is set to -1 and the maturityHeight to the opposite value
                self.check_certificate_maturity_info(cert, -1, "SUPERSEDED", (current_block + blocks_to_maturity) * (-1))

            # Check immature certificates (top quality)
            for cert in [cert2_sc1, cert2_sc2]:
                self.check_certificate_maturity_info(cert, blocks_to_maturity, "IMMATURE", current_block + blocks_to_maturity)

            # Check mature certificates (for non-ceasing sidechain)
            for cert in [cert1_sc3, cert2_sc3]:
                self.check_certificate_maturity_info(cert, 0, "MATURE", certificate_including_block_height)

        mark_logs("Revert one more block to send all the certificates to mempool", self.nodes, DEBUG_MODE)
        reverting_block_hash = self.nodes[0].getbestblockhash()
        self.nodes[0].invalidateblock(reverting_block_hash)

        for cert in [cert1_sc1, cert1_sc2]:
            self.check_certificate_maturity_info(cert, -1, "LOW_QUALITY_MEMPOOL", -1)

        for cert in [cert1_sc3, cert2_sc1, cert2_sc2, cert2_sc3]:
            self.check_certificate_maturity_info(cert, -1, "TOP_QUALITY_MEMPOOL", -1)

        mark_logs("Node 0 generates blocks to include certificates and reach the end of submission window", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(blocks_to_maturity)

        mark_logs("Node 0 creates certificates to avoid ceasing of sc1 and sc2", self.nodes, DEBUG_MODE)
        cert3_sc1: str = test_helper.send_certificate(v1_sc1_name, 10) # Epoch 1
        cert3_sc2: str = test_helper.send_certificate(v2_ceasing_sc2_name, 10) # Epoch 1
        cert3_sc3: str = test_helper.send_certificate(v2_non_ceasing_sc3_name, 0) # Epoch 3

        mark_logs("Check that the certificates for sc1 and sc2 are maturing in the next block", self.nodes, DEBUG_MODE)
        for cert in [cert2_sc1, cert2_sc2]:
            self.check_certificate_maturity_info(cert, 1, "IMMATURE", current_block + blocks_to_maturity)

        mark_logs("Check that the certificate for sc3 is still MATURE", self.nodes, DEBUG_MODE)
        self.check_certificate_maturity_info(cert2_sc3, 0, "MATURE", certificate_including_block_height)

        mark_logs("Check that the certificates in the mempool are not confirmed yet", self.nodes, DEBUG_MODE)
        for cert in [cert3_sc1, cert3_sc2, cert3_sc3]:
            self.check_certificate_maturity_info(cert, -1, "TOP_QUALITY_MEMPOOL", -1)

        mark_logs("Node 0 generates one block to include certificates and close the submission window", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)

        # Calculate the new maturity heights for the certificates taking into account that we are currently
        # on the first block AFTER the end of the submission window for epoch 0 (that is the 3rd block of epoch 1).
        current_height: int = self.nodes[0].getblockcount()
        epoch_0_maturity_height = current_height # For epoch 0, certificates just matured
        epoch_1_maturity_height = current_height + epoch_length # For epoch 1, certificates will mature after the next submission window
        blocks_to_maturity = epoch_length

        mark_logs("Check that sc1 and sc2 low quality certificates for epoch 0 are still superseded", self.nodes, DEBUG_MODE)
        for cert in [cert1_sc1, cert1_sc2]:
            self.check_certificate_maturity_info(cert, -1, "SUPERSEDED", epoch_0_maturity_height * (-1))

        mark_logs("Check that the sc1 and sc2 certificates for epoch 0 have matured", self.nodes, DEBUG_MODE)
        for cert in [cert2_sc1, cert2_sc2]:
            self.check_certificate_maturity_info(cert, 0, "MATURE", epoch_0_maturity_height)

        mark_logs("Check that the sc1 and sc2 certificates for epoch 1 are immature", self.nodes, DEBUG_MODE)
        for cert in [cert3_sc1, cert3_sc2]:
            self.check_certificate_maturity_info(cert, blocks_to_maturity, "IMMATURE", epoch_1_maturity_height)

        mark_logs("Check that all the certificates for sc3 (non-ceasing) are mature", self.nodes, DEBUG_MODE)
        for cert in [cert1_sc3, cert2_sc3]:
            self.check_certificate_maturity_info(cert, 0, "MATURE", certificate_including_block_height)
        
        self.check_certificate_maturity_info(cert3_sc3, 0, "MATURE", current_height)


if __name__ == '__main__':
    sc_getcertmaturityinfo().main()
