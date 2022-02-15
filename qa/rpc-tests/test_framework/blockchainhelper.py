#!/usr/bin/env python2

from decimal import Decimal

from authproxy import JSONRPCException
from mc_test.mc_test import CertTestUtils, CSWTestUtils
from util import mark_logs

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
            "sc_id": creation_response["scid"]
        }

    def get_sidechain_creation_input(self, sc_name, sidechain_version):
        return {
            "version": sidechain_version,
            "withdrawalEpochLength": 10,
            "toaddress": "abcd",
            "amount":  Decimal("1.0"),
            "wCertVk": self.cert_utils.generate_params(sc_name),
        }

    def get_sidechain_id(self, sc_name):
        #sc_id = self.nodes[0].getrawtransaction(self.sidechain_map[sc_name]["creation_tx_id"], 1)['vsc_ccout'][0]['scid']
        #self.sidechain_map[sc_name]["sc_id"] = sc_id
        #return sc_id
        return self.sidechain_map[sc_name]["sc_id"]
