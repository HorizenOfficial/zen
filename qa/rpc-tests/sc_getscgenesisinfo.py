#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs
from test_framework.blockchainhelper import BlockchainHelper, SidechainParameters

DEBUG_MODE = 1
NUMB_OF_NODES = 2


class sc_getscgenesisinfo(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-scproofqueuesize=0', '-logtimemicros=1']] * NUMB_OF_NODES)

        for k in range(0, NUMB_OF_NODES-1):
            connect_nodes_bi(self.nodes, k, k+1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def get_genesis_sidechain_versions(self, sidechain_id, creation_block_hash):

        # Get the genesis information of the sidechain
        genesis_info = self.nodes[0].getscgenesisinfo(sidechain_id)

        # Get the raw genesis block
        block_hex = self.nodes[0].getblock(creation_block_hash, 0)

        # Since the genesis info includes the raw block, we can use it to easily locate the list of sidechain versions
        # (the list is immediately after the serialized block)
        sidechain_version_pair_list = genesis_info.split(block_hex)[1]

        # The first byte (2 hex digits) is the size of the sidechain versions array
        list_length = int(sidechain_version_pair_list[0:2], 16)

        sidechain_version_pair_list = sidechain_version_pair_list[2:]
        pair_serialized_size = 33 * 2 # 32 bytes for the sidechain id and 1 byte for the sidechain version, each byte displayed as 2 hex digits
        assert_equal(len(sidechain_version_pair_list), list_length * pair_serialized_size)

        sc_versions = {}

        for i in range(0, list_length):

            # Extract the sidechain id from the serialized pair
            little_endian_scid = sidechain_version_pair_list[i * pair_serialized_size : (i + 1) * pair_serialized_size - 2]

            # Since the sidechain id is serialized in little endian, we need to reverse the bytes for later comparison
            scid_bytes = bytearray.fromhex(little_endian_scid)
            scid_bytes.reverse()
            scid_string = ''.join(format(x, '02x') for x in scid_bytes)
            
            # Extract the sidechain version from the serialized pair
            sc_version = int(sidechain_version_pair_list[(i + 1) * pair_serialized_size - 2 : (i + 1) * pair_serialized_size], 16)

            # Add extracted data to a map
            sc_versions[scid_string] = sc_version

        return sc_versions

    def run_test(self):

        ''' 
        This test is intended to test that the getscgenesisinfo RPC command works properly.
        For doing this, we create the following sidechains:

         - sc1 with version 0
         - sc2 with version 1
         - sc3 with version 2 (ceasing)
         - sc4 with version 2 (non-ceasing)

        Then after few blocks we publish certificates for them and at the same time we create:

         - sc5 with version 1
         - sc6 with version 2 (non-ceasing)

        In this way, the getscgenesisinfo RPC command should return the list of versions for
        the sidechains (sc1, sc2, sc3, and sc4) that published the certificates in the same
        block used for the creation of sidechain 5 and 6.
        '''

        # Reach the sidechain version fork point
        test_helper = BlockchainHelper(self)
        self.nodes[0].generate(ForkHeights['NON_CEASING_SC'])

        mark_logs("Node 0 creates a v0 sidechain", self.nodes, DEBUG_MODE)
        v0_sc1_name = "v0_sc1"
        test_helper.create_sidechain(v0_sc1_name, SidechainParameters["DEFAULT_SC_V0"])

        mark_logs("Node 0 creates a v1 sidechain", self.nodes, DEBUG_MODE)
        v1_sc2_name = "v1_sc2"
        test_helper.create_sidechain(v1_sc2_name, SidechainParameters["DEFAULT_SC_V1"])

        mark_logs("Node 0 creates a v2 ceasing sidechain", self.nodes, DEBUG_MODE)
        v2_sc3_name = "v2_sc3_ceasing"
        test_helper.create_sidechain(v2_sc3_name, SidechainParameters["DEFAULT_SC_V2_CEASABLE"])

        mark_logs("Node 0 creates a v2 non-ceasing sidechain", self.nodes, DEBUG_MODE)
        v2_sc4_name = "v2_sc4_non_ceasing"
        test_helper.create_sidechain(v2_sc4_name, SidechainParameters["DEFAULT_SC_V2_NON_CEASABLE"])

        self.sync_all()

        mark_logs("Node 0 generates a block to confirm the creation of sidechains", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        epoch_length = test_helper.sidechain_map[v0_sc1_name]["creation_args"].withdrawalEpochLength

        mark_logs(f"Node 0 generates {epoch_length - 1} blocks to reach end of epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(epoch_length - 1)
        self.sync_all()

        mark_logs("Node 0 creates a fifth sidechain (v1)", self.nodes, DEBUG_MODE)
        v1_sc5_name = "v1_sc5"
        test_helper.create_sidechain(v1_sc5_name, SidechainParameters["DEFAULT_SC_V1"])
        self.sync_all()

        mark_logs("Node 0 creates a sixth sidechain (v2 non-ceasing)", self.nodes, DEBUG_MODE)
        v2_sc6_name = "v2_sc6_non_ceasing"
        test_helper.create_sidechain(v2_sc6_name, SidechainParameters["DEFAULT_SC_V2_NON_CEASABLE"])
        self.sync_all()

        mark_logs("Node 0 creates a certificate for sidechain 1, 2, and 3", self.nodes, DEBUG_MODE)
        test_helper.send_certificate(v0_sc1_name, 10)
        test_helper.send_certificate(v1_sc2_name, 10)
        test_helper.send_certificate(v2_sc3_name, 10)
        test_helper.send_certificate(v2_sc4_name, 0) # Non-ceasing sidechain
        self.sync_all()

        mark_logs("Node 0 generates a block including the creation of sidechain 3 and the cerficates", self.nodes, DEBUG_MODE)
        last_block_hash = self.nodes[0].generate(1)[0]
        self.sync_all()

        mark_logs("Check correctness of the genesis sidechain info for sc5", self.nodes, DEBUG_MODE)
        versions = self.get_genesis_sidechain_versions(test_helper.sidechain_map[v1_sc5_name]["sc_id"], last_block_hash)

        # Check that the genesis sidechain info contains exactly two sidechain version entries
        assert_equal(len(versions), 4)

        # Check that the genesis sidechain info contains the correct sidechain version for sidechain 1
        assert_equal(versions[test_helper.sidechain_map[v0_sc1_name]["sc_id"]], 0)

        # Check that the genesis sidechain info contains the correct sidechain version for sidechain 2
        assert_equal(versions[test_helper.sidechain_map[v1_sc2_name]["sc_id"]], 1)

        # Check that the genesis sidechain info contains the correct sidechain version for sidechain 3
        assert_equal(versions[test_helper.sidechain_map[v2_sc3_name]["sc_id"]], 2)

        # Check that the genesis sidechain info contains the correct sidechain version for sidechain 4 (non-ceasing)
        assert_equal(versions[test_helper.sidechain_map[v2_sc4_name]["sc_id"]], 2)

        # The test is over, the following lines are only intended to be used as reference for SDK tests
        mark_logs("Print raw block and sidechain commitment root for cross checks with SDK", self.nodes, DEBUG_MODE)
        raw_block = self.nodes[0].getblock(last_block_hash, 0)
        json_block = self.nodes[0].getblock(last_block_hash, 1)
        sc_txs_commitment_tree_root = json_block["scTxsCommitment"]
        print("")
        print("Block raw hex:")
        print(raw_block)
        print("")
        print("Block sc txs commitment tree root: {}".format(sc_txs_commitment_tree_root))

        print(f"ID sidechain 1: { test_helper.sidechain_map[v0_sc1_name]['sc_id'] }")
        print(f"ID sidechain 2: { test_helper.sidechain_map[v1_sc2_name]['sc_id'] }")
        print(f"ID sidechain 3: { test_helper.sidechain_map[v2_sc3_name]['sc_id'] }")
        print(f"ID sidechain 4: { test_helper.sidechain_map[v2_sc4_name]['sc_id'] }")
        print(f"ID sidechain 5: { test_helper.sidechain_map[v1_sc5_name]['sc_id'] }")
        print(f"ID sidechain 6: { test_helper.sidechain_map[v2_sc6_name]['sc_id'] }")


if __name__ == '__main__':
    sc_getscgenesisinfo().main()
