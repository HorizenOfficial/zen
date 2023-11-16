#!/usr/bin/env python3
# Copyright (c) 2017 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_node, connect_nodes_bi, wait_and_assert_operationid_status, download_snapshot
import zipfile
from decimal import Decimal

class WalletMergeToAddress2Test (BitcoinTestFramework):

    op_result_k = "op_result"
    balance_delta_k = "balance_delta"
    joinsplit_len_k = "joinsplit_len"
    mergingUTXOs_k = "mergingUTXOs"
    mergingNotes_k = "mergingNotes"
    mergingTransparentValue_k = "mergingTransparentValue"
    mergingShieldedValue_k = "mergingShieldedValue"

    def import_data_to_data_dir(self):
        
        # importing datadir resource
        #
        # 100 blocks generated on node0 + 100 blocks generated on node1 + (1 "shielding" on node0 + 1 block generated on node1) * 100
        #
        # node0:
        # +] ztmVazvU5wC6cbizPinHMU5GjJYj1iKxhTH                                                             -> 0.00000000
        # +] zthRUUZDq5yiToeHpi2R5qAAhs19SSw6pSs                                                             -> 0.00000000
        # +] ztmPTt93ugMaijAghGncX8ir6hPLvwQThz8N2K5uBcjU8E58qXQmne5Rcp4taxRTFpWDZPyv673p7e6aHLkjza4ywe88AkW -> 1143.74000000 {100 notes}
        #
        # node1:
        # +] ztbQADKRubhMhrrpxA2JQR6VLzssWZntQju                                                             -> 0.00000000
        # +] ztmSek66GPEPc6z2HyumX6ypmqYAaiSCuvD                                                             -> 882.75000000 (+750.01000000 immature) {100 utxos (+100 immature)}
        #

        snapshot_filename = 'wallet_mergetoaddress_2_NP_snapshot.zip'
        resource_file = download_snapshot(snapshot_filename, self.options.tmpdir)
        with zipfile.ZipFile(resource_file, 'r') as zip_ref:
            zip_ref.extractall(self.options.tmpdir)

    def get_merged_transaction(self, unspent_transactions_before_merge, unspent_transactions_after_merge):
        unspent_txid_before = [x["txid"] for x in unspent_transactions_before_merge]
        for txid in [x["txid"] for x in unspent_transactions_after_merge]:
            if txid not in unspent_txid_before:
                return self.nodes[2].gettransaction(txid)
        return None

    def z_mergetoaddress_z2z(self, notes_quantity):
        if (len(self.nodes[2].z_listaddresses()) == 0):
            myzaddr2 = self.nodes[2].z_getnewaddress()
        else:
            myzaddr2 = self.nodes[2].z_listaddresses()[0]
        listunspent_before_merge = self.nodes[2].z_listunspent(0)
        balance_before_merge = self.nodes[2].z_getbalance(myzaddr2)
        result = self.nodes[0].z_mergetoaddress(["ANY_ZADDR"], myzaddr2, 0, 0, notes_quantity)
        wait_and_assert_operationid_status(self.nodes[0], result['opid'], "success", "", 3600)
        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()
        tx = self.get_merged_transaction(listunspent_before_merge, self.nodes[2].z_listunspent(0))
        return {self.op_result_k: result, self.balance_delta_k: self.nodes[2].z_getbalance(myzaddr2) - balance_before_merge, self.joinsplit_len_k: len(tx["vjoinsplit"])}

    def setup_chain(self):
        self.import_data_to_data_dir()
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_network(self, split=False):
        args = ['-debug=zrpcunsafe', '-experimentalfeatures', '-zmergetoaddress', '-maxtipage=3153600000'] # 60 * 60 * 24 * 365 * 100
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, args))
        self.nodes.append(start_node(1, self.options.tmpdir, args))
        self.nodes.append(start_node(2, self.options.tmpdir, args))
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        self.is_network_split=False
        self.sync_all()

    def run_test (self):

        '''
        The test leverages a predefined setup with many available notes to call a merge of a high quantity of notes in a single note of a z-addr.
        The purpose of the test is to check the actual number of notes targeted by the async operation is smaller than the limit set by the user.
        This result witnesses the transaction estimation is working properly, eventually avoiding the transaction to include too many notes thus
        ending up being rejected due to unacceptable size
        '''

        # request merging of up to 60 notes (available more than 60, but transaction size limit admits up to 58) from node0 to node2
        result = self.z_mergetoaddress_z2z(60)
        assert_equal(result[self.op_result_k][self.mergingUTXOs_k], 0)
        assert_equal(result[self.op_result_k][self.mergingNotes_k], 58)
        assert_equal(result[self.op_result_k][self.mergingTransparentValue_k], 0)
        # 663.3692 is the sum of the amount of the first 58 notes
        assert_equal(result[self.op_result_k][self.mergingShieldedValue_k], Decimal("663.36920000"))
        assert_equal(result[self.balance_delta_k], Decimal("663.36920000"))
        # 57 joinsplits are required because first two shielded inputs are merged in a shielded output, which is then set as shielded input (change) which is
        # then set as shielded input (change) in the subsequent joinsplit and paired with another shielded input until the final shielded output is obtained
        # note0  _____
        # note1  _____|____
        # note2  __________|____
        # note3  _______________|
        # .
        # .
        # .
        # note56 ____________________..._____|____
        # note57 ____________________...__________|
        assert_equal(result[self.joinsplit_len_k], 57)

if __name__ == '__main__':
    WalletMergeToAddress2Test().main()
