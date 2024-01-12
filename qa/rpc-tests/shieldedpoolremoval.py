#!/usr/bin/env python3
# Copyright (c) 2016 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


from test_framework.test_framework import BitcoinTestFramework, ForkHeights
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_greater_than, \
    assert_greater_or_equal_than, initialize_chain_clean, start_nodes, \
    connect_nodes, wait_and_assert_operationid_status, gather_inputs, \
    send_shielding_raw, send_unshielding_raw, send_shielded_raw, \
    download_snapshot
from json import loads
from decimal import Decimal
from zipfile import ZipFile

RPC_VERIFY_REJECTED = -26
RPC_HARD_FORK_DEPRECATION = -40
Z_FEE = 0.0001

class ShieldedPoolDeprecationTest (BitcoinTestFramework):

    raw_notes_and_keys = [None] * 4

    def import_data_to_data_dir(self):
        # importing datadir resource
        snapshot_filename = 'shieldedpoolremoval_snapshot.zip'
        resource_file = download_snapshot(snapshot_filename, self.options.tmpdir)
        with ZipFile(resource_file, 'r') as zip_ref:
            zip_ref.extractall(self.options.tmpdir)
        with open(self.options.tmpdir + '/raw_notes/raw_notes.json', 'r') as file:
            raw_notes_and_keys = loads(file.read())
        for item in raw_notes_and_keys:
            item[0]["amount"] = (Decimal)(item[0]["amount"])
        return raw_notes_and_keys

    def setup_chain(self):
        self.raw_notes_and_keys = self.import_data_to_data_dir()
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 1)

    def setup_network(self, split=False):
        self.nodes = start_nodes(1, self.options.tmpdir, [['-debug=zrpcunsafe', '-experimentalfeatures', '-zmergetoaddress', '-maxtipage=3153600000']])
        self.is_network_split=False
        self.sync_all()

    def make_all_mature(self):
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

    def run_test (self):

        ForkHeight = ForkHeights['SHIELDED_POOL_REMOVAL']

        # This part has been used for generating the snapshot and is commented as currently not in use
        """
        import os
        import json

        ForkHeight_SPD = ForkHeights['SHIELDED_POOL_DEPRECATION']
        print("Mining blocks...")
        self.nodes[0].generate(ForkHeight_SPD - 2)
        self.sync_all()

        node0_taddr1 = self.nodes[0].getnewaddress()
        node0_zaddr0 = self.nodes[0].z_getnewaddress()

        # generating some shielded notes
        notes_to_create = 13
        for repeat in range(notes_to_create):
            opid = self.nodes[0].z_shieldcoinbase("*", node0_zaddr0, Z_FEE, 2)["opid"]
            wait_and_assert_operationid_status(self.nodes[0], opid)
            self.sync_all()
        # generating some shielded notes through raw interface
        notes_to_create_raw = 4
        minimum_amount = 1.0
        self.raw_notes_and_keys = [None] * notes_to_create_raw
        for i in range(notes_to_create_raw):
            zckeypair = self.nodes[0].zcrawkeygen()
            zcsecretkey = zckeypair["zcsecretkey"]
            zcaddress = zckeypair["zcaddress"]
            tx_hash, encrypted_note = send_shielding_raw(self.nodes[0], minimum_amount, zcaddress)
            self.raw_notes_and_keys[i] = [self.nodes[0].zcrawreceive(zcsecretkey, encrypted_note), zcsecretkey]
            self.raw_notes_and_keys[i][0]["amount"] = (float)(self.raw_notes_and_keys[i][0]["amount"])
            self.sync_all()
        raw_notes_and_keys_index = 0

        newpath = self.options.tmpdir + '/raw_notes/'
        if not os.path.exists(newpath):
            os.makedirs(newpath)
        with open(self.options.tmpdir + '/raw_notes/raw_notes.json', 'w') as file:
            file.write(json.dumps(self.raw_notes_and_keys)) # use `json.loads` to do the reverse

        blockcount = self.nodes[0].getblockcount()
        self.nodes[0].generate(ForkHeight - blockcount - 2 - 8) # (8 make_all_mature))
        self.sync_all()
        """

        # addresses
        node0_taddr0 = self.nodes[0].listaddresses()[0]
        node0_zaddr0 = self.nodes[0].z_listaddresses()[0]

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

            # z/tz->t
            sourceaddresses = ["*", "ANY_ZADDR", node0_zaddr0]
            for sourceaddress in sourceaddresses:
                try:
                    opid = self.nodes[0].z_mergetoaddress([sourceaddress], self.nodes[0].getnewaddress(), Z_FEE, 2, 2)["opid"]
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

            # z/tz->z
            sourceaddresses = ["ANY_ZADDR", node0_zaddr0]
            for sourceaddress in sourceaddresses:
                try:
                    opid = self.nodes[0].z_mergetoaddress([sourceaddress], node0_zaddr0, Z_FEE, 2, 2)["opid"]
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
            try:
                opid = self.nodes[0].z_mergetoaddress(["*"], node0_zaddr0, Z_FEE, 2, 2)["opid"]
                assert(False)
            except JSONRPCException as e:
                print("Expected exception caught during testing due to deprecation (error=" + str(e.error["code"]) + ")")
                assert_equal(e.error["code"], RPC_HARD_FORK_DEPRECATION)

            self.make_all_mature()

            # t/z->z
            sourceaddresses = ["*", "ANY_TADDR", node0_taddr0]
            for sourceaddress in sourceaddresses:
                try:
                    opid = self.nodes[0].z_mergetoaddress([sourceaddress], node0_zaddr0, Z_FEE, 2, 2)["opid"]
                    assert(False)
                except JSONRPCException as e:
                    print("Expected exception caught during testing due to deprecation (error=" + str(e.error["code"]) + ")")
                    assert_equal(e.error["code"], RPC_HARD_FORK_DEPRECATION)

            self.make_all_mature()

            # t->t
            sourceaddresses = ["ANY_TADDR", node0_taddr0]
            for sourceaddress in sourceaddresses:
                try:
                    opid = self.nodes[0].z_mergetoaddress([sourceaddress], self.nodes[0].getnewaddress(), Z_FEE, 2, 0)["opid"]
                    wait_and_assert_operationid_status(self.nodes[0], opid)
                    self.sync_all()
                except JSONRPCException as e:
                    print("Unexpected exception caught during testing: " + str(e.error))
                    assert(False)
            try:
                opid = self.nodes[0].z_mergetoaddress(["*"], self.nodes[0].getnewaddress(), Z_FEE, 2, 2)["opid"]
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


            # z_sendmany
            print(f"z_sendmany ({'pre-fork' if round == pre_fork_round else 'post-fork'})")

            # z->t, z->tz, z->z
            destinationaddresses = [
                                    [{"address": self.nodes[0].getnewaddress(), "amount": 1.0}],
                                    [{"address": self.nodes[0].getnewaddress(), "amount": 1.0},
                                     {"address": node0_zaddr0, "amount": 1.0}],
                                    [{"address": node0_zaddr0, "amount": 1.0}]
                                   ]
            for destinationaddress in destinationaddresses:
                try:
                    opid = self.nodes[0].z_sendmany(node0_zaddr0, destinationaddress, 1, Z_FEE)
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

            # t->tz, t->z, t->t
            destinationaddresses = [
                                    [{"address": self.nodes[0].getnewaddress(), "amount": 1.0},
                                     {"address": node0_zaddr0, "amount": 1.0}],
                                    [{"address": node0_zaddr0, "amount": 1.0}]
                                   ]
            for destinationaddress in destinationaddresses:
                try:
                    opid = self.nodes[0].z_sendmany(node0_taddr0, destinationaddress, 1, Z_FEE)
                    assert(False)
                except JSONRPCException as e:
                    print("Expected exception caught during testing due to deprecation (error=" + str(e.error["code"]) + ")")
                    assert_equal(e.error["code"], RPC_HARD_FORK_DEPRECATION)
            try:
                opid = self.nodes[0].z_sendmany(node0_taddr0, [{"address": node0_taddr0, "amount": 1.0}], 1, Z_FEE)
                wait_and_assert_operationid_status(self.nodes[0], opid)
                self.sync_all()
            except JSONRPCException as e:
                print("Unexpected exception caught during testing: " + str(e.error))
                assert(False)

            self.make_all_mature()


            # raw
            print(f"createrawtransaction/zcrawjoinsplit ({'pre-fork' if round == pre_fork_round else 'post-fork'})")
            raw_notes_and_keys_index = 0

            # z->t, z->z, z->tz
            for tx_type in ["z->t", "z->z", "z->tz"]:
                try:
                    raw_note_and_key_to_use = self.raw_notes_and_keys[raw_notes_and_keys_index]
                    if (tx_type == "z->t"):
                        send_unshielding_raw(self.nodes[0], raw_note_and_key_to_use[0], raw_note_and_key_to_use[1], self.nodes[0].getnewaddress())
                    elif (tx_type == "z->z"):
                        send_shielded_raw(self.nodes[0], raw_note_and_key_to_use[0], raw_note_and_key_to_use[1],
                                           {}, {node0_zaddr0: raw_note_and_key_to_use[0]["amount"]})
                    elif (tx_type == "z->tz"):
                        outamount = raw_note_and_key_to_use[0]["amount"]
                        send_shielded_raw(self.nodes[0], raw_note_and_key_to_use[0], raw_note_and_key_to_use[1],
                                          {self.nodes[0].getnewaddress(): outamount / 2}, {node0_zaddr0: outamount / 2})
                    if (round == post_fork_round):
                        assert(False)
                    raw_notes_and_keys_index += 1
                    self.sync_all()
                except JSONRPCException as e:
                    if (round == pre_fork_round):
                        print("Unexpected exception caught during testing: " + str(e.error))
                        assert(False)
                    else:
                        print("Expected exception caught during testing due to deprecation (error=" + str(e.error["code"]) + ")")
                        assert_equal(e.error["code"], RPC_VERIFY_REJECTED)

            self.make_all_mature()

            # t->z, t->tz, t->t
            for tx_type in ["t->z", "t->tz"]:
                try:
                    raw_note_and_key_to_use = self.raw_notes_and_keys[raw_notes_and_keys_index]
                    if (tx_type == "t->z"):
                        send_shielding_raw(self.nodes[0], 1.0, node0_zaddr0)
                    elif (tx_type == "t->tz"):
                        send_shielding_raw(self.nodes[0], 1.0, node0_zaddr0, t_recipients = {self.nodes[0].getnewaddress(): 0.5})
                    assert(False)
                except JSONRPCException as e:
                    print("Expected exception caught during testing due to deprecation (error=" + str(e.error["code"]) + ")")
                    assert_equal(e.error["code"], RPC_VERIFY_REJECTED)
            try:
                (total_in, inputs) = gather_inputs(self.nodes[0], 1.0)
                transparent_tx = self.nodes[0].createrawtransaction(inputs, {self.nodes[0].getnewaddress(): total_in})
                transparent_tx = self.nodes[0].signrawtransaction(transparent_tx)
                transparent_tx_id = self.nodes[0].sendrawtransaction(transparent_tx["hex"])
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
