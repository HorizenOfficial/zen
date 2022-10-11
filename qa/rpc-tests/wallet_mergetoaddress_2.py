#!/usr/bin/env python3
# Copyright (c) 2017 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import shutil
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_node, connect_nodes_bi, sync_blocks, sync_mempools, \
    wait_and_assert_operationid_status

import time
from decimal import Decimal
from distutils.dir_util import copy_tree

class WalletMergeToAddressTest (BitcoinTestFramework):

    op_result_k = "op_result"
    balance_delta_k = "balance_delta"
    joinsplit_len_k = "joinsplit_len"
    mergingUTXOs_k = "mergingUTXOs"
    mergingNotes_k = "mergingNotes"
    mergingTransparentValue_k = "mergingTransparentValue"
    mergingShieldedValue_k = "mergingShieldedValue"

    def import_data_to_data_dir(self):
        #importing datadir resource
        import os
        os.chdir(os.path.dirname(__file__))
        resource_file = os.getcwd() + "/resources/wallet_mergetoaddress_2/test_setup_.zip"
        import zipfile
        with zipfile.ZipFile(resource_file, 'r') as zip_ref:
            zip_ref.extractall(self.options.tmpdir)
        copy_tree(self.options.tmpdir + "/test_setup_", self.options.tmpdir)
        shutil.rmtree(self.options.tmpdir + "/test_setup_")

    def generate_notes(self, quantity):
        myzaddr0 = self.nodes[0].z_getnewaddress()
        results = []
        for i in range(quantity):
            self.nodes[0].generate(1)
            self.sync_all()
            self.nodes[1].generate(100)
            self.sync_all()
            results.append(self.nodes[0].z_shieldcoinbase("*", myzaddr0, 0))
            wait_and_assert_operationid_status(self.nodes[0], results[i]['opid'])
        self.nodes[1].generate(1)
        self.sync_all()

    def send_transparent(self, quantity):
        for i in range(quantity):
            self.nodes[1].sendmany("", {self.nodes[0].listaddresses()[0]: 10}, 100)
        self.nodes[1].generate(100)
        self.sync_all

    def get_merged_transaction(self, unspent_transactions_before_merge, unspent_transactions_after_merge):
        txid = unspent_transactions_after_merge[0]["txid"]
        for aft in range(len(unspent_transactions_after_merge)):
            for bef in range(len(unspent_transactions_before_merge)):
                if (unspent_transactions_after_merge[aft]["txid"] == unspent_transactions_before_merge[bef]["txid"]):
                    break
                if (bef == len(unspent_transactions_before_merge) - 1):
                    txid = unspent_transactions_after_merge[aft]["txid"]
        return self.nodes[2].gettransaction(txid)

    def z_mergetoaddress_t2t(self, utxos_quantity):
        mytaddr2 = self.nodes[2].listaddresses()[0]
        listunspent_before_merge = self.nodes[2].listunspent(0)
        balance_before_merge = self.nodes[2].z_getbalance(mytaddr2)
        result = self.nodes[0].z_mergetoaddress(["ANY_TADDR"], mytaddr2, 0, utxos_quantity, 0)        
        wait_and_assert_operationid_status(self.nodes[0], result['opid'], "success", "", 3600)
        self.nodes[1].generate(100)
        self.sync_all()
        tx = self.get_merged_transaction(listunspent_before_merge, self.nodes[2].listunspent(0))
        return {self.op_result_k: result, self.balance_delta_k: self.nodes[2].z_getbalance(mytaddr2) - balance_before_merge, self.joinsplit_len_k: len(tx["vjoinsplit"])}

    def z_mergetoaddress_t2z(self, utxos_quantity):
        if (len(self.nodes[2].z_listaddresses()) == 0):
            myzaddr2 = self.nodes[2].z_getnewaddress()
        else:
            myzaddr2 = self.nodes[2].z_listaddresses()[0]
        listunspent_before_merge = self.nodes[2].z_listunspent(0)
        balance_before_merge = self.nodes[2].z_getbalance(myzaddr2)
        result = self.nodes[0].z_mergetoaddress(["ANY_TADDR"], myzaddr2, 0, utxos_quantity, 0)        
        wait_and_assert_operationid_status(self.nodes[0], result['opid'], "success", "", 3600)
        self.nodes[1].generate(100)
        self.sync_all()
        tx = self.get_merged_transaction(listunspent_before_merge, self.nodes[2].z_listunspent(0))
        return {self.op_result_k: result, self.balance_delta_k: self.nodes[2].z_getbalance(myzaddr2) - balance_before_merge, self.joinsplit_len_k: len(tx["vjoinsplit"])}

    def z_mergetoaddress_z2t(self, notes_quantity):
        mytaddr2 = self.nodes[2].listaddresses()[0]
        listunspent_before_merge = self.nodes[2].listunspent(0)
        balance_before_merge = self.nodes[2].z_getbalance(mytaddr2)
        result = self.nodes[0].z_mergetoaddress(["ANY_ZADDR"], mytaddr2, 0, 0, notes_quantity)        
        wait_and_assert_operationid_status(self.nodes[0], result['opid'], "success", "", 3600)
        self.nodes[1].generate(100)
        self.sync_all()
        tx = self.get_merged_transaction(listunspent_before_merge, self.nodes[2].listunspent(0))
        return {self.op_result_k: result, self.balance_delta_k: self.nodes[2].z_getbalance(mytaddr2) - balance_before_merge, self.joinsplit_len_k: len(tx["vjoinsplit"])}

    def z_mergetoaddress_z2z(self, notes_quantity):
        if (len(self.nodes[2].z_listaddresses()) == 0):
            myzaddr2 = self.nodes[2].z_getnewaddress()
        else:
            myzaddr2 = self.nodes[2].z_listaddresses()[0]
        listunspent_before_merge = self.nodes[2].z_listunspent(0)
        balance_before_merge = self.nodes[2].z_getbalance(myzaddr2)
        result = self.nodes[0].z_mergetoaddress(["ANY_ZADDR"], myzaddr2, 0, 0, notes_quantity)        
        wait_and_assert_operationid_status(self.nodes[0], result['opid'], "success", "", 3600)
        self.nodes[1].generate(100)
        self.sync_all()
        tx = self.get_merged_transaction(listunspent_before_merge, self.nodes[2].z_listunspent(0))
        return {self.op_result_k: result, self.balance_delta_k: self.nodes[2].z_getbalance(myzaddr2) - balance_before_merge, self.joinsplit_len_k: len(tx["vjoinsplit"])}

    def z_mergetoaddress_tz2t(self, utxos_quantity, notes_quantity):
        mytaddr2 = self.nodes[2].listaddresses()[0]
        listunspent_before_merge = self.nodes[2].listunspent(0)
        balance_before_merge = self.nodes[2].z_getbalance(mytaddr2)
        result = self.nodes[0].z_mergetoaddress(["*"], mytaddr2, 0, utxos_quantity, notes_quantity)        
        wait_and_assert_operationid_status(self.nodes[0], result['opid'], "success", "", 3600)
        self.nodes[1].generate(100)
        self.sync_all()
        tx = self.get_merged_transaction(listunspent_before_merge, self.nodes[2].listunspent(0))
        return {self.op_result_k: result, self.balance_delta_k: self.nodes[2].z_getbalance(mytaddr2) - balance_before_merge, self.joinsplit_len_k: len(tx["vjoinsplit"])}

    def z_mergetoaddress_tz2z(self, utxos_quantity, notes_quantity):
        if (len(self.nodes[2].z_listaddresses()) == 0):
            myzaddr2 = self.nodes[2].z_getnewaddress()
        else:
            myzaddr2 = self.nodes[2].z_listaddresses()[0]
        listunspent_before_merge = self.nodes[2].z_listunspent(0)
        balance_before_merge = self.nodes[2].z_getbalance(myzaddr2)
        result = self.nodes[0].z_mergetoaddress(["*"], myzaddr2, 0, utxos_quantity, notes_quantity)        
        wait_and_assert_operationid_status(self.nodes[0], result['opid'], "success", "", 3600)
        self.nodes[1].generate(100)
        self.sync_all()
        tx = self.get_merged_transaction(listunspent_before_merge, self.nodes[2].z_listunspent(0))
        return {self.op_result_k: result, self.balance_delta_k: self.nodes[2].z_getbalance(myzaddr2) - balance_before_merge, self.joinsplit_len_k: len(tx["vjoinsplit"])}

    def setup_chain(self):
        self.import_data_to_data_dir()
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_network(self, split=False):
        args = ['-debug=zrpcunsafe', '-experimentalfeatures', '-zmergetoaddress']
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, args))
        self.nodes.append(start_node(1, self.options.tmpdir, args))
        args2 = ['-debug=zrpcunsafe', '-experimentalfeatures', '-zmergetoaddress', '-mempooltxinputlimit=7']
        self.nodes.append(start_node(2, self.options.tmpdir, args2))
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        self.is_network_split=False
        self.sync_all()

    def run_test (self):

        # Node0 acts as "from" (merge_to_address sender)
        # Node1 acts as miner and funds distributor
        # Node2 acts ad "to" (merge_to_address receiver)

        #(generate 1b on n0, generate 100bs on node1, shield 1 b on n0) * 100 + generate 1b on n1
        #this code is provided if there is need to extend setup datadir
        #self.generate_notes(100)
        #assert_equal(self.nodes[0].getblockcount(), 10101)
        #assert_equal(self.nodes[0].getwalletinfo()["balance"] +
        #             self.nodes[0].getwalletinfo()["unconfirmed_balance"] +
        #             self.nodes[0].getwalletinfo()["immature_balance"], 0)
 

        # ---------- T to T ----------      
        #move 3 utxos from n1 to n0
        self.send_transparent(3)
        #merge up to 3 utxos (available more than 3) from n0 to n2
        result = self.z_mergetoaddress_t2t(3)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 3)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 0)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k], result[self.balance_delta_k])
        assert_equal(result[self.op_result_k][self.mergingShieldedValue_k], 0)
        #no joinsplit is required because every input is transparent and every output is transparent
        assert_equal(result[self.joinsplit_len_k], 0)

        #move 4 utxos from n1 to n0
        self.send_transparent(4)
        #merge up to 4 utxos (available more than 4) from n0 to n2
        result = self.z_mergetoaddress_t2t(4)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 4)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 0)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k], result[self.balance_delta_k])
        assert_equal(result[self.op_result_k][self.mergingShieldedValue_k], 0)
        #no joinsplit is required because every input is transparent and every output is transparent
        assert_equal(result[self.joinsplit_len_k], 0)
        # ---------- T to T ----------


        # ---------- T to Z ----------
        #move 3 utxos from n1 to n0
        self.send_transparent(3)
        #merge up to 3 utxos (available more than 3) from n0 to n2
        result = self.z_mergetoaddress_t2z(3)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 3)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 0)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k], result[self.balance_delta_k])
        assert_equal(result[self.op_result_k][self.mergingShieldedValue_k], 0)
        #only 1 joinsplit is required because every input is transparent and there is only a single shielded output
        assert_equal(result[self.joinsplit_len_k], 1)

        #move 4 utxos from n1 to n0
        self.send_transparent(4)
        #merge up to 4 utxos (available more than 4) from n0 to n2
        result = self.z_mergetoaddress_t2z(4)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 4)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 0)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k], result[self.balance_delta_k])
        assert_equal(result[self.op_result_k][self.mergingShieldedValue_k], 0)
        #only 1 joinsplit is required because every input is transparent and there is only a single shielded output
        assert_equal(result[self.joinsplit_len_k], 1)
        # ---------- T to Z ----------


        # ---------- Z to T ----------
        #merge up to 3 notes (available more than 3) from n0 to n2
        result = self.z_mergetoaddress_z2t(3)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 0)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 3)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k], 0)
        assert_equal(result[self.op_result_k][self.mergingShieldedValue_k], result[self.balance_delta_k])
        #2 joinsplits are required because first two shielded inputs are merged in a shielded output,
        #which is then set as shielded input (change) in the subsequent joinsplit and paired with another shielded input
        #until the final shielded output is obtained; eventually, the last joinsplit contains the total amount of funds
        #flowing back to public pool
        assert_equal(result[self.joinsplit_len_k], 2)

        #merge up to 4 notes (available more than 4) from n0 to n2
        result = self.z_mergetoaddress_z2t(4)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 0)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 4)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k], 0)
        assert_equal(result[self.op_result_k][self.mergingShieldedValue_k], result[self.balance_delta_k])
        #3 joinsplits are required because first two shielded inputs are merged in a shielded output,
        #which is then set as shielded input (change) in the subsequent joinsplit and paired with another shielded input
        #until the final shielded output is obtained; eventually, the last joinsplit contains the total amount of funds
        #flowing back to public pool
        assert_equal(result[self.joinsplit_len_k], 3)
        # ---------- Z to T ----------
        

        # ---------- Z to Z ----------
        #merge up to 3 notes (available more than 3) from n0 to n2
        result = self.z_mergetoaddress_z2z(3)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 0)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 3)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k], 0)
        assert_equal(result[self.op_result_k][self.mergingShieldedValue_k], result[self.balance_delta_k])
        #2 joinsplits are required because first two shielded inputs are merged in a shielded output,
        #which is then set as shielded input (change) in the subsequent joinsplit and paired with another shielded input
        #until the final shielded output is obtained
        assert_equal(result[self.joinsplit_len_k], 2)

        #merge up to 4 notes (available more than 4) from n0 to n2
        result = self.z_mergetoaddress_z2z(4)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 0)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 4)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k], 0)
        assert_equal(result[self.op_result_k][self.mergingShieldedValue_k], result[self.balance_delta_k])
        #3 joinsplits are required because first two shielded inputs are merged in a shielded output,
        #which is then set as shielded input (change) in the subsequent joinsplit and paired with another shielded input
        #until the final shielded output is obtained
        assert_equal(result[self.joinsplit_len_k], 3)
        # ---------- Z to Z ----------


        # ---------- TZ to T ----------
        #move 3 utxos from n1 to n0
        self.send_transparent(3)
        #merge up to 3 utxos (available more than 3) and up to 3 notes (available more than 3) from n0 to n2
        result = self.z_mergetoaddress_tz2t(3, 3)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 3)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 3)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k] + result[self.op_result_k][self.mergingShieldedValue_k], result[self.balance_delta_k])
        #number of joinsplits analogous to t2t + z2t
        assert_equal(result[self.joinsplit_len_k], 2)

        #move 4 utxos from n1 to n0
        self.send_transparent(4)
        #merge up to 4 utxos (available more than 4) and up to 4 notes (available more than 4) from n0 to n2
        result = self.z_mergetoaddress_tz2t(4, 4)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 4)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 4)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k] + result[self.op_result_k][self.mergingShieldedValue_k], result[self.balance_delta_k])
        #number of joinsplits analogous to t2t + z2t
        assert_equal(result[self.joinsplit_len_k], 3)
        # ---------- TZ to T ----------


        # ---------- TZ to Z ----------
        #move 3 utxos from n1 to n0
        self.send_transparent(3)
        #merge up to 3 utxos (available more than 3) and up to 3 notes (available more than 3) from n0 to n2
        result = self.z_mergetoaddress_tz2z(3, 3)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 3)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 3)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k] + result[self.op_result_k][self.mergingShieldedValue_k], result[self.balance_delta_k])
        #number of joinsplits analogous to t2z + z2z, but consider the joinsplit required by t2z is actually collpased in the final z2z joinsplit
        assert_equal(result[self.joinsplit_len_k], 2)

        #move 4 utxos from n1 to n0
        self.send_transparent(4)
        #merge up to 4 utxos (available more than 4) and up to 4 notes (available more than 4) from n0 to n2
        result = self.z_mergetoaddress_tz2z(4, 4)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 4)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 4)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k] + result[self.op_result_k][self.mergingShieldedValue_k], result[self.balance_delta_k])
        #number of joinsplits analogous to t2z + z2z, but consider the joinsplit required by t2z is actually collpased in the final z2z joinsplit
        assert_equal(result[self.joinsplit_len_k], 3)
        # ---------- TZ to Z ----------


        # ---------- big size ----------
        #merge up to 60 notes (available more than 60, but transaction size limit admits up to 58) from n0 to n2
        result = self.z_mergetoaddress_z2z(60)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 0)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 58)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k], 0)
        assert_equal(result[self.op_result_k][self.mergingShieldedValue_k], result[self.balance_delta_k])
        #57 joinsplits are required because first two shielded inputs are merged in a shielded output,
        #which is then set as shielded input (change) in the subsequent joinsplit and paired with another shielded input
        #until the final shielded output is obtained
        assert_equal(result[self.joinsplit_len_k], 57)

        #merge up to 60 notes (available only 14) from n0 to n2
        result = self.z_mergetoaddress_z2z(60)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 0)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 14)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k], 0)
        assert_equal(result[self.op_result_k][self.mergingShieldedValue_k], result[self.balance_delta_k])
        #13 joinsplits are required because first two shielded inputs are merged in a shielded output,
        #which is then set as shielded input (change) in the subsequent joinsplit and paired with another shielded input
        #until the final shielded output is obtained
        assert_equal(result[self.joinsplit_len_k], 13)
        # ---------- big size ----------

if __name__ == '__main__':
    WalletMergeToAddressTest().main()
