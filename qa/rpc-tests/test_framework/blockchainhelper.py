#!/usr/bin/env python3

from decimal import Decimal

from authproxy import JSONRPCException
from mc_test.mc_test import CertTestUtils, CSWTestUtils, generate_random_field_element_hex
from util import mark_logs, swap_bytes

# A helper class to accomplish some operations in a faster way
# (e.g. sidechain creation).

EXPECT_SUCCESS = False
EXPECT_FAILURE = True

class BlockchainHelper(object):

    def __init__(self, bitcoin_test_framework):
        self.sidechain_map = {}
        self.init_proof_keys(bitcoin_test_framework)
        self.nodes = bitcoin_test_framework.nodes

    def init_proof_keys(self, bitcoin_test_framework):
        self.cert_utils = CertTestUtils(bitcoin_test_framework.options.tmpdir, bitcoin_test_framework.options.srcdir)
        self.csw_utils = CSWTestUtils(bitcoin_test_framework.options.tmpdir, bitcoin_test_framework.options.srcdir)

    # sc_name is not the real sidechain id (it will be generated automatically during the call to sc_create)
    # but an identifier used to:
    # 1) search for the sidechain in the sidechain map
    # 2) generate the certificate and CSW verification keys
    def create_sidechain(self, sc_name, sidechain_version, should_fail = False):
        sc_input = self.get_sidechain_creation_input(sc_name, sidechain_version)
        error_message = None

        try:
            ret = self.nodes[0].sc_create(sc_input)
            assert(not should_fail)
            self.store_sidechain(sc_name, sc_input, ret)
        except JSONRPCException as e:
            error_message = e.error['message']
            mark_logs(error_message, self.nodes, 1)
            assert(should_fail)

        return error_message

    def store_sidechain(self, sc_name, sc_params, creation_response):
        self.sidechain_map[sc_name] = {
            "name": sc_name,
            "version": sc_params["version"],
            "cert_vk": sc_params["wCertVk"],
            "creation_tx_id": creation_response["txid"],
            "sc_id": creation_response["scid"],
            "withdrawalEpochLength": sc_params["withdrawalEpochLength"],
            "constant": sc_params["constant"]
        }

    def get_sidechain_creation_input(self, sc_name, sidechain_version):
        return {
            "version": sidechain_version,
            "withdrawalEpochLength": 10,
            "toaddress": "abcd",
            "amount":  Decimal("1.0"),
            "wCertVk": self.cert_utils.generate_params(sc_name),
            "constant": generate_random_field_element_hex()
        }

    def get_sidechain_id(self, sc_name):
        return self.sidechain_map[sc_name]["sc_id"]

    def send_certificate(self, sc_name, quality):
        sc_info = self.sidechain_map[sc_name]
        scid = sc_info["sc_id"]
        withdrawalEpochLength = sc_info["withdrawalEpochLength"]
        constant = sc_info["constant"]

        sc_creation_height = self.nodes[0].getscinfo(scid)['items'][0]['createdAtBlockHeight']
        current_height = self.nodes[0].getblockcount()
        epoch_number = (current_height - sc_creation_height + 1) // withdrawalEpochLength - 1
        end_epoch_block_hash = self.nodes[0].getblockhash(sc_creation_height - 1 + ((epoch_number + 1) * withdrawalEpochLength))
        epoch_cum_tree_hash = self.nodes[0].getblock(end_epoch_block_hash)['scCumTreeHash']
        scid_swapped = str(swap_bytes(scid))
        
        proof = self.cert_utils.create_test_proof(sc_name, scid_swapped, epoch_number, quality, 0, 0, epoch_cum_tree_hash, constant)

        if proof is None:
            print("Unable to create proof")
            assert(False)

        certificate_id = self.nodes[0].sc_send_certificate(scid, epoch_number, quality, epoch_cum_tree_hash, proof, [], 0, 0)

        return certificate_id
