#!/usr/bin/env python3
import codecs
from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, get_epoch_data,\
    start_nodes, connect_nodes_bi, stop_node, wait_and_assert_operationid_status, swap_bytes
import os
import zipfile
import time
from test_framework.test_framework import MINIMAL_SC_HEIGHT, MINER_REWARD_POST_H200
from test_framework.mc_test.mc_test import *

class OversizedTx(BitcoinTestFramework):
    FEE = 0.0001

    def import_data_to_data_dir(self):

        # importing datadir resource
        #
        # 101 blocks generated on node2 + (0.001 sent from node2 to node0 + 0.001 sent from node2 to node1 + 1 block generated on node2) * 2000
        #
        # node0:
        # +] ztfu9KvBM5ef7aoRXZrfXsQM6BBPwduDo5N -> 2.00000000 {2000 utxos}
        #
        # node1:
        # +] ztTEv3uMRhWoKmBmqTdap54AiyCRRitPYNm -> 2.00000000 {2000 utxos}
        #
        # node2:
        # +] ztonWoE4zHCtWwE1SMZKY2hPCgfkkghWENy
		# +] ztTAABJcZ2E2Cgg7HyUzqBsibF7fzwvjGnk
        # +] ...
        # +] ztksKwGeJUpx5KcWTodM1FE4r4rinMnXLT8 -> tot 15522.49940000 (+375.00060000 immature) {2001 utxos (+100 immature)}

        resource_file = os.sep.join([os.path.dirname(__file__), 'resources', 'oversized_tx', 'test_setup_.zip'])
        with zipfile.ZipFile(resource_file, 'r') as zip_ref:
            zip_ref.extractall(self.options.tmpdir)

    def setup_chain(self):
        self.import_data_to_data_dir()
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_network(self, split=False):
        self.nodes = start_nodes(3, self.options.tmpdir, extra_args=[['-debug=zrpcunsafe', '-maxtipage=36000000']] * 3 )
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        self.is_network_split=False
        self.sync_all()

    def swap_bytes(input_buf):
        return codecs.encode(codecs.decode(input_buf, 'hex')[::-1], 'hex').decode()

    def run_test (self):

        nt0 = self.nodes[0].listaddresses()[0]
        nt1 = self.nodes[1].listaddresses()[0]
        nt2 = self.nodes[2].listaddresses()[0]
        nt2bis = self.nodes[2].getnewaddress()

        nt0b = self.nodes[0].z_getbalance(nt0)
        nt1b = self.nodes[1].z_getbalance(nt1)
        nt2b = self.nodes[2].z_getbalance(nt2)

        print("N0 -> (" + nt0 + " , " + str(nt0b) + ")")
        print("N1 -> (" + nt1 + " , " + str(nt1b) + ")")
        print("N2 -> (" + nt2 + " , " + str(nt2b) + ")")


        # ---------- testing sendto (sendfrom is analogous) ----------
        # a middle value coin is sent which will be later used in next transaction (together with other coins)
        # no oversized transaction error is expected ("JSONRPC error: Transaction too large")
        self.nodes[2].sendtoaddress(nt0, 0.5)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        self.nodes[0].sendtoaddress(nt2, 0.7)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        try:
            # no middle value coin is sent, hence it will be impossible (due to oversize) to create next transaction
            # using only small coins
            self.nodes[1].sendtoaddress(nt2, 0.7)
            assert(False) # impossibile to create transaction (as expected)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Impossible to create transaction (as expected): {}".format(errorString))
        # a big value coin is sent which will be later used in next transaction as the only coin because it would be
        # impossible (due to oversize) to create next transaction using only small coins
        self.nodes[2].sendtoaddress(nt1, 1.0)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        self.nodes[1].sendtoaddress(nt2, 0.7)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        # a big value coin is sent which will be later used in next transaction as the only coin because small coins
        # don't sum up to target value
        self.nodes[2].sendtoaddress(nt1, 3.0)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        self.nodes[1].sendtoaddress(nt2, 2.5)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()


        # ---------- testing sendmany ----------
        # three middle value coins are sent which will be later used in next transaction (together with other coins)
        # no oversized transaction error is expected ("JSONRPC error: Transaction too large")
        self.nodes[2].sendtoaddress(nt0, 0.5)
        self.nodes[2].sendtoaddress(nt0, 0.5)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        self.nodes[0].sendmany("", {nt2: 0.7, nt2bis: 0.7}, 0, "", [])
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        # two middle value coins are sent but will not suffice for creating next transaction (even if mixed with other small coins)
        self.nodes[2].sendtoaddress(nt0, 0.5)
        self.nodes[2].sendtoaddress(nt0, 0.5)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        try:
            # no middle value coin is sent, hence it will be impossible (due to oversize) to create next transaction
            # using only small coins
            self.nodes[0].sendmany("", {nt2: 1.0, nt2bis: 1.0}, 0, "", [])
            assert(False) # impossibile to create transaction (as expected)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Impossible to create transaction (as expected): {}".format(errorString))

if __name__ == '__main__':
        OversizedTx().main()