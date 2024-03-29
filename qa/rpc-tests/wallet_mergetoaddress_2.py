#!/usr/bin/env python3
# Copyright (c) 2017 The Zcash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_node, connect_nodes_bi, wait_and_assert_operationid_status
import zipfile
import requests
import io

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
        # +] ztpTDK62DWALH9GWJu4rPgqihEGxoWAHFUq                                                             -> 0.00000000
        # +] ztZwyCWWmUvHsP1Ho1rB8sjHTC81y93n4rF                                                             -> 0.00000000
        # +] ztTw9aP9jeWRNsR3uMHEB3Z3VUqdFsmJaUVWLgJZDRXQM3MydY56JVr8mXC21kUBgJ51YGKK8QuYSVx2kSM7YXf9gZm1Rb4 -> 1143.75000000 {100 notes}
        #
        # node1:
        # +] ztpvEirKWj1mzvoMCn718KkM2DA1nXohuew                                                             -> 0.00000000
        # +] ztiFyseKx9q7h4D9eXMjygQqbHvLA4FTZfT                                                             -> 882.75000000 (+750.75000000 immature) {100 utxos (+100 immature)}
        #
        
        r = requests.get("https://downloads.horizen.io/file/depends-sources/test_setup_wallet_mergetoaddress_2.zip", stream=True)
        if r.ok:
            z = zipfile.ZipFile(io.BytesIO(r.content))
            z.extractall(self.options.tmpdir)
        else:
            raise Exception("Failed to download zip archive for data dir setup (" + str(r.status_code) + " - " + r.reason + ")")

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
        # 663.375 is the sum of the amount of the first 58 notes
        assert_equal(result[self.op_result_k][self.mergingShieldedValue_k], 663.375)
        assert_equal(result[self.balance_delta_k], 663.375)
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
