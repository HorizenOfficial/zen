#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import pprint
from decimal import Decimal

from test_framework.authproxy import JSONRPCException
from test_framework.mc_test.mc_test import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.util import initialize_chain_clean, \
    sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, assert_true, assert_false, start_node, \
    stop_nodes, wait_bitcoinds

DEBUG_MODE = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH = 5


class ScCrFwtStop(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        for i in range(0, NUMB_OF_NODES):
            if i == 0:
                # in the node 0 set a regtest flag for skipping the checks in the RPC commands.
                # This allows to test the consensus rules without blocking the rpc commands
                self.nodes += [start_node(i, self.options.tmpdir,
                                          extra_args=['-debug=py', '-debug=sc', '-logtimemicros=1',
                                                      '-skipscopforkcheck=1'])]
            else:
                self.nodes += [start_node(i, self.options.tmpdir,
                                          extra_args=['-debug=py', '-debug=sc', '-logtimemicros=1'])]

        for k in range(0, NUMB_OF_NODES - 1):
            connect_nodes_bi(self.nodes, k, k + 1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        """
        Test that after reaching the proper fork height, it is not possible to create a new sidechain or to do a fwd
        transfer to an existing one. More in details:
        - Reach a chain height 1 block before the fork point
        - Create a new SC so that the tx goes into the mempool
        - Split the network:
           . network 0: a block is generated and the SC is created. The SC info can be retrieved
           . network 1: 2 blocks are generated so to get a longer chain
        - Join the network
           . The network 0 SC creation block is reverted, the relevant SC creation tx gets back to the mempool but is
             evicted since the fork point has been reached
           . The SC info is not retrievable anymore and no SC creation tx can be found in the mempool
        - Try creating a new SC and try to fwt to existing SC: it is not possible anymore after the fork point
        """

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()

        # sidechain creation
        # -------------------
        creation_amount = Decimal("1.0")
        # generate wCertVk and constant
        mc_test = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        cmd_input = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": mc_test.generate_params("sc1"),
            "constant": generate_random_field_element_hex(),
            "minconf": 0
        }

        ret = self.nodes[0].sc_create(cmd_input)
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

        current_height = self.nodes[0].getblockcount()
        mark_logs(("active chain height = %d: testing before sidechain fork" % current_height), self.nodes, DEBUG_MODE)

        # reach the height where the next block is the last before the fork point
        delta = ForkHeights['STOP_SC_CR_AND_FWDT'] - current_height - 2

        mark_logs("Node 1 generates {} block for reaching a pre-fork point, where no new SC creations "
                  "will be allowed as well as fw transfers to existing ones".format(delta), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(delta)
        self.sync_all()
        current_height = self.nodes[0].getblockcount()
        mark_logs(("active chain height = %d" % current_height), self.nodes, DEBUG_MODE)

        mark_logs("---------------- Split the network --------------", self.nodes, DEBUG_MODE)
        self.split_network(0)

        # ----------------------Network Part 0
        mark_logs("Node 0 creates SC2", self.nodes, DEBUG_MODE)
        cmd_input = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": mc_test.generate_params("sc2"),
            "constant": generate_random_field_element_hex()
        }
        ret = self.nodes[0].sc_create(cmd_input)
        creating_tx = ret['txid']

        mark_logs("Check creating tx={} is in node 0 mempool...".format(creating_tx), self.nodes, DEBUG_MODE)
        assert_true(creating_tx in self.nodes[0].getrawmempool())

        mark_logs("Node 0 generates 1 block", self.nodes, DEBUG_MODE)
        bl = self.nodes[0].generate(1)[0]
        pprint.pprint(self.nodes[0].getblock(bl))

        mark_logs("Check we do have the new SC in network part 0", self.nodes, DEBUG_MODE)
        sc_info_pre = self.nodes[0].getscinfo(ret['scid'])
        pprint.pprint(sc_info_pre)
        assert_true(1 == len(sc_info_pre['items']))

        # ----------------------Network Part 1
        mark_logs("Node 1 generates 2 blocks, crossing the fork point and reaching the longest chain", self.nodes,
                  DEBUG_MODE)
        self.nodes[1].generate(2)

        mark_logs("---------------------- Join the network -------------------", self.nodes, DEBUG_MODE)
        self.join_network(0)
        self.sync_all()

        sc_info_post = self.nodes[0].getscinfo(ret['scid'])

        mark_logs("Check we do not have the new SC in the joint network", self.nodes, DEBUG_MODE)
        pprint.pprint(sc_info_post)
        assert_true(0 == len(sc_info_post['items']))

        mark_logs("Check tx={} is not in mempool...".format(creating_tx), self.nodes, DEBUG_MODE)
        assert_false(creating_tx in self.nodes[0].getrawmempool())
        assert_false(creating_tx in self.nodes[1].getrawmempool())

        mark_logs("Node 0 tries to create a new SC, expecting error", self.nodes, DEBUG_MODE)
        try:
            self.nodes[0].sc_create(cmd_input)
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "16: bad-tx-sc-creation-fwdt-stopped"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        mark_logs("Node 0 tries to send " + str(fwt_amount) + " coins to SC1, expecting error", self.nodes, DEBUG_MODE)
        try:
            self.nodes[0].sc_send(ft_cmd_input)
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "16: bad-tx-sc-creation-fwdt-stopped"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        self.sync_all()

        mark_logs("Node 1 tries to create a new SC, expecting error", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].sc_create(cmd_input)
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "This method is disabled"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        mark_logs("Node 1 tries to send " + str(fwt_amount) + " coins to SC1, expecting error", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].sc_send(ft_cmd_input)
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "This method is disabled"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        self.sync_all()

        mark_logs("Node 1 tries to create a new SC via a raw tx, expecting error", self.nodes, DEBUG_MODE)
        sc_cr = [{
            "version": 0,
            "epoch_length": EPOCH_LENGTH,
            "amount": creation_amount,
            "address": "dada",
            "wCertVk": mc_test.generate_params("sc3"),
            "constant": generate_random_field_element_hex()
        }]

        try:
            self.nodes[1].createrawtransaction([], {}, [], sc_cr, [])
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "This method is disabled"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        mark_logs("Node 1 tries to send " + str(fwt_amount) + " coins to SC1 via raw tx, expecting error", self.nodes,
                  DEBUG_MODE)
        sc_ft = [{
            "address": "abc",
            "amount": fwt_amount,
            "scid": scid,
            "mcReturnAddress": mc_return_address
        }]

        try:
            self.nodes[1].createrawtransaction([], {}, [], [], sc_ft)
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "This method is disabled"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        self.sync_all()




if __name__ == '__main__':
    ScCrFwtStop().main()
