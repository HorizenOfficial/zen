#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from decimal import Decimal

from test_framework.authproxy import JSONRPCException
from test_framework.mc_test.mc_test import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.util import initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs

DEBUG_MODE = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH = 5

class sc_cr_fw_stop(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False, bool_flag=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-deprecatedgetblocktemplate=%d'%bool_flag, '-logtimemicros=1']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        Test that after reaching the proper fork height, it is not possible to create a new sidechain or to do a fwd
        transfer to an existing one.
        '''

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()

        # sidechain creation
        #-------------------
        creation_amount = Decimal("1.0")
        # generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": constant,
            "minconf": 0
        }

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        self.sync_all()
        mark_logs("Node0 created SC scid={}, tx={}".format(scid, creating_tx), self.nodes, DEBUG_MODE)

        fwt_amount = Decimal("2.0")

        mark_logs("Node 1 sends " + str(fwt_amount) + " coins to SC", self.nodes, DEBUG_MODE)
        mc_return_address = self.nodes[1].getnewaddress()
        ft_cmd_input = [{'toaddress': "abcd", 'amount': fwt_amount, "scid": scid, 'mcReturnAddress': mc_return_address}]
        self.nodes[1].sc_send(ft_cmd_input)
        self.sync_all()

        mark_logs("Node 1 generates {} block for reaching fork point where no new SC creations are allowed as well as "
                  "fw transfers to existing ones".format(ForkHeights['STOP_SC_CR_AND_FWDT']), self.nodes, DEBUG_MODE)
        self.nodes[1].generate(ForkHeights['STOP_SC_CR_AND_FWDT'])
        self.sync_all()

        mark_logs("Node 0 tries to create SC 2 with " + str(creation_amount) + " coins", self.nodes, DEBUG_MODE)
        amounts = []
        amounts.append({"toaddress": "dada", "amount": creation_amount})

        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": 123,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": mcTest.generate_params("sc2"),
            "constant": generate_random_field_element_hex()
        }

        try:
            self.nodes[0].sc_create(cmdInput)
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)
            expected_substring = "16: bad-tx-sc-creation-fwdt-stopped"
            assert expected_substring in errorString, f"'{errorString}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        mark_logs("Node 0 tries to send " + str(fwt_amount) + " coins to SC 1", self.nodes, DEBUG_MODE)

        try:
            self.nodes[0].sc_send(ft_cmd_input)
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)
            expected_substring = "16: bad-tx-sc-creation-fwdt-stopped"
            assert expected_substring in errorString, f"'{errorString}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        self.sync_all()


if __name__ == '__main__':
    sc_cr_fw_stop().main()
