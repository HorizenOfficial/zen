#!/usr/bin/env python3
# Copyright (c) 2016 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


from test_framework.test_framework import BitcoinTestFramework, ForkHeights
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_greater_than, \
    assert_greater_or_equal_than, initialize_chain_clean, start_nodes, \
    connect_nodes, wait_and_assert_operationid_status, \
    send_shielding_raw, send_unshielding_raw

import sys

RPC_VERIFY_REJECTED = -26
RPC_HARD_FORK_DEPRECATION = -40
Z_FEE = 0.0001

class ShieldedPoolDeprecationTest (BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 2)

    def setup_network(self, split=False):
        self.nodes = start_nodes(2, self.options.tmpdir, [['-debug=zrpcunsafe', '-experimentalfeatures', '-zmergetoaddress']] * 2)
        connect_nodes(self.nodes, 0, 1)
        self.is_network_split=False
        self.sync_all()

    def make_all_mature(self):
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

    def run_test (self):

        ForkHeight_SPD = ForkHeights['SHIELDED_POOL_DEPRECATION']
        ForkHeight = ForkHeights['UNSHIELDING_TO_SCRIPT_ONLY']

        print("Mining blocks...")

        self.nodes[0].generate(ForkHeight_SPD - 10)
        self.sync_all()

        # addresses
        script_address = "zr3EMnGsEtpqTVbBdo4RztB3RQfNMUXt63j" # (0x 2092 0123456789012345678901234567890123456789 94a14d8a)
        node0_zaddr0 = self.nodes[0].z_getnewaddress()

        # generating some shielded notes
        notes_to_create = 15
        for repeat in range(notes_to_create):
            opid = self.nodes[0].z_shieldcoinbase("*", node0_zaddr0, Z_FEE, 2)["opid"]
            wait_and_assert_operationid_status(self.nodes[0], opid)
            self.sync_all()
        # generating some shielded notes through raw interface
        notes_to_create_raw = 3
        minimum_amount = 1.0
        receive_result_and_key = [None] * notes_to_create_raw
        for i in range(notes_to_create_raw):
            zckeypair = self.nodes[0].zcrawkeygen()
            zcsecretkey = zckeypair["zcsecretkey"]
            zcaddress = zckeypair["zcaddress"]
            tx_hash, encrypted_note = send_shielding_raw(self.nodes[0], minimum_amount, zcaddress)
            receive_result_and_key[i] = [self.nodes[0].zcrawreceive(zcsecretkey, encrypted_note), zcsecretkey]
            self.sync_all()
        receive_result_and_key_index = 0

        self.nodes[0].generate(1)
        self.sync_all()
        
        # first round pre-fork, second round post-fork
        pre_fork_round = 0
        post_fork_round = 1
        for round in range(2):
            blockcount = self.nodes[0].getblockcount()
            if (round == pre_fork_round):
                assert_greater_than(ForkHeight, blockcount + 1) # otherwise following tests would make no sense
                                                                # +1 because a new tx would enter the blockchain on next mined block
            elif (round == post_fork_round):
                assert_equal(blockcount + 1, ForkHeight) # otherwise following tests would make no sense


            # z_mergetoaddress
            print(f"z_mergetoaddress ({'pre-fork' if round == pre_fork_round else 'post-fork'})")

            try:
                opid = self.nodes[0].z_mergetoaddress(["ANY_ZADDR"], self.nodes[0].getnewaddress(), Z_FEE, 0, 2)["opid"]
                if (round == post_fork_round):
                    assert(False)
                wait_and_assert_operationid_status(self.nodes[0], opid)
                self.sync_all()
            except JSONRPCException as e:
                if (round == pre_fork_round):
                    print("Unexpected exception caught during testing: " + str(e.error))
                    assert(False)
                else:
                    print("Expected exception caught during testing due to deprecation (error=" + str(e.error["code"]) + ")")
                    assert_equal(e.error["code"], RPC_HARD_FORK_DEPRECATION)

            self.make_all_mature()

            try:
                opid = self.nodes[0].z_mergetoaddress(["ANY_ZADDR"], script_address, Z_FEE, 0, 2)["opid"]
                wait_and_assert_operationid_status(self.nodes[0], opid)
                self.sync_all()
            except JSONRPCException as e:
                print("Unexpected exception caught during testing: " + str(e.error))
                assert(False)

            self.make_all_mature()

            try:
                opid = self.nodes[0].z_mergetoaddress(["ANY_ZADDR"], self.nodes[0].z_getnewaddress(), Z_FEE, 0, 2)["opid"]
                wait_and_assert_operationid_status(self.nodes[0], opid)
                self.sync_all()
            except JSONRPCException as e:
                print("Unexpected exception caught during testing: " + str(e.error))
                assert(False)

            self.make_all_mature()

            # z_sendmany
            print(f"z_sendmany ({'pre-fork' if round == pre_fork_round else 'post-fork'})")

            try:
                opid = self.nodes[0].z_sendmany(node0_zaddr0, [{"address": self.nodes[0].getnewaddress(), "amount": 1.0}], 1, Z_FEE)
                if (round == post_fork_round):
                    assert(False)
                wait_and_assert_operationid_status(self.nodes[0], opid)
                self.sync_all()
            except JSONRPCException as e:
                if (round == pre_fork_round):
                    print("Unexpected exception caught during testing: " + str(e.error))
                    assert(False)
                else:
                    print("Expected exception caught during testing due to deprecation (error=" + str(e.error["code"]) + ")")
                    assert_equal(e.error["code"], RPC_HARD_FORK_DEPRECATION)

            self.make_all_mature()

            try:
                opid = self.nodes[0].z_sendmany(node0_zaddr0, [{"address": script_address, "amount": 1.0}], 1, Z_FEE)
                wait_and_assert_operationid_status(self.nodes[0], opid)
                self.sync_all()
            except JSONRPCException as e:
                print("Unexpected exception caught during testing: " + str(e.error))
                assert(False)

            self.make_all_mature()

            try:
                opid = self.nodes[0].z_sendmany(node0_zaddr0, [{"address": self.nodes[0].z_getnewaddress(), "amount": 1.0}], 1, Z_FEE)
                wait_and_assert_operationid_status(self.nodes[0], opid)
                self.sync_all()
            except JSONRPCException as e:
                print("Unexpected exception caught during testing: " + str(e.error))
                assert(False)

            self.make_all_mature()

            # raw
            print(f"createrawtransaction/zcrawjoinsplit ({'pre-fork' if round == pre_fork_round else 'post-fork'})")

            try:
                receive_result_and_key_to_use = receive_result_and_key[receive_result_and_key_index]
                send_unshielding_raw(self.nodes[0], receive_result_and_key_to_use[0], receive_result_and_key_to_use[1], self.nodes[0].getnewaddress())
                if (round == post_fork_round):
                    assert(False)
                receive_result_and_key_index += 1
                self.sync_all()
            except JSONRPCException as e:
                if (round == pre_fork_round):
                    print("Unexpected exception caught during testing: " + str(e.error))
                    assert(False)
                else:
                    print("Expected exception caught during testing due to deprecation (error=" + str(e.error["code"]) + ")")
                    assert_equal(e.error["code"], RPC_VERIFY_REJECTED)

            self.make_all_mature()

            try:
                receive_result_and_key_to_use = receive_result_and_key[receive_result_and_key_index]
                send_unshielding_raw(self.nodes[0], receive_result_and_key_to_use[0], receive_result_and_key_to_use[1], script_address)
                receive_result_and_key_index += 1
                self.sync_all()
            except JSONRPCException as e:
                print("Unexpected exception caught during testing: " + str(e.error))
                assert(False)

            self.make_all_mature()


            blockcount = self.nodes[0].getblockcount()
            if (round == pre_fork_round):
                assert_greater_than(ForkHeight, blockcount + 1) # otherwise previous tests would make no sense
            elif (round == post_fork_round):
                assert_greater_or_equal_than(blockcount, ForkHeight) # otherwise previous tests would make no sense

            if (round == pre_fork_round):
                self.nodes[0].generate(ForkHeight - blockcount - 1)
                self.sync_all()


if __name__ == '__main__':
    ShieldedPoolDeprecationTest().main()
