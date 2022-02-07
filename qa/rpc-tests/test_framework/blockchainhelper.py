#!/usr/bin/env python2

from decimal import Decimal

from authproxy import JSONRPCException
from mc_test.mc_test import CertTestUtils, CSWTestUtils
from util import mark_logs

# An helper class to accomplish some operations in a faster way
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

    def create_sidechain(self, sc_id, sidechain_version, should_fail = False):
        sc_input = self.get_sidechain_creation_input(sc_id, sidechain_version)
        error_message = None

        try:
            self.nodes[0].sc_create(sc_input)
            assert(not should_fail)
        except JSONRPCException as e:
            error_message = e.error['message']
            mark_logs(error_message, self.nodes, 1)
            assert(should_fail)

        # TODO: store the sidechain information in self.sidechain_map so that we can easily perform other operations
        # in the future (e.g. certificate creation, forwards transfers, backward transfers, and ceased sidechain withdrawals).

        return error_message

    def get_sidechain_creation_input(self, sc_id, sidechain_version):
        return {
            "version": sidechain_version,
            "withdrawalEpochLength": 10,
            "toaddress": "abcd",
            "amount":  Decimal("1.0"),
            "wCertVk": self.cert_utils.generate_params(sc_id),
        }
