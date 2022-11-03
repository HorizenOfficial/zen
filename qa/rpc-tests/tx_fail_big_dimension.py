#!/usr/bin/env python3
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, connect_nodes_bi, stop_node, wait_and_assert_operationid_status
import os
import zipfile
import time

class ZSendmanyTest(BitcoinTestFramework):
    FEE = 0.0001

    def import_data_to_data_dir(self):
        
        # importing datadir resource
        #
        # (1 block generated on node0, 100 blocks generated on node1, 1 block "shielded" on node0) * 100 + 1 block generated on node1 + (1 block generated on node0, 100 blocks generated on node1, 1 block "shielded" on node0) * 20 + 1 block generated on node1 
        #
        # node0:
        # +] ztgrYDXWWXzk8Rb7wN13S97MLP3pcBuvica                                                             -> 0.00000000
        # +] zthqePHnzxgcqvBmSWXLvJeoKWfV6mzd2UU                                                             -> 0.00000000
        # +] ztMccAcM5nNtDbwNinaydTzCFG1RP7wQN5kWdkU39YD4tByFCiEZs2KKr9pyfMdi2ZLfoF1s8ZMyFVr5fUZVx3riA5hWN1y -> 297.82812500 {100 notes}
        #
        # node1:
        # +] ztYnpAb9cgkNezFsnbydxbV1WumA89vAxxv                                                             -> 0.00000000
        # +] ztWVGvLNSLxy7gioSrRqXHuUgEmrNd9THVB                                                             -> 29750.54687500 (+11.71875000 immature) {11902 utxos (+100 immature)}
        #
        # node2:
        # +] ztZTd1erG3X1me9zyPjjZjRXHSq14dVRwF6                                                             -> 0.00000000

        resource_file = os.sep.join([os.path.dirname(__file__), 'resources', 'tx_fail_big_dimension', 'test_setup_.zip'])
        with zipfile.ZipFile(resource_file, 'r') as zip_ref:
            zip_ref.extractall(self.options.tmpdir)

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
            self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()

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

    def run_test (self):

        test_flow = 0 # sendtoaddress
        test_flow = 1 # sendfrom
        test_flow = 2 # sendmany
        test_flow = 3 # z_sendmany(z)
        test_flow = 4 # z_sendmany(t)

        test_flow = 1

        nt0 = self.nodes[0].listaddresses()[0]
        nz0 = self.nodes[0].z_listaddresses()[0]
        nt1 = self.nodes[1].listaddressgroupings()[0][0][0]
        nt2 = self.nodes[2].listaddresses()[0]

        nt0b = self.nodes[0].z_getbalance(nt0)
        nz0b = self.nodes[0].z_getbalance(nz0)
        nt1b = self.nodes[1].z_getbalance(nt1)
        nt2b = self.nodes[2].z_getbalance(nt2)

        print("N0 -> (" + nt0 + " , " + str(nt0b) + ")")
        print("N0 -> (" + nz0 + " , " + str(nz0b) + ")")
        print("N1 -> (" + nt1 + " , " + str(nt1b) + ")")
        print("N2 -> (" + nt2 + " , " + str(nt2b) + ")")

        if test_flow == 0:
            self.nodes[1].sendtoaddress(nt0, nt1b)
        
        elif test_flow == 1:
            self.nodes[1].sendfrom("", nt0, nt1b)

        elif test_flow == 2:
            self.nodes[1].sendmany("", {nt0: nt1b}, 0, "", [])

        elif test_flow == 3:
            recipients= [{"address":nt2, "amount": nz0b}]
            myopid = self.nodes[0].z_sendmany(nz0, recipients, 1, 0, True)
            txid = wait_and_assert_operationid_status(self.nodes[0], myopid, "success", "", 3600)

        elif test_flow == 4:
            recipients= [{"address":nz0, "amount": nt1b}]
            myopid = self.nodes[1].z_sendmany(nt1, recipients, 1, 0, True)
            txid = wait_and_assert_operationid_status(self.nodes[1], myopid, "success", "", 3600)

        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()

        nt0b = self.nodes[0].z_getbalance(nt0)
        nz0b = self.nodes[0].z_getbalance(nz0)
        nt1b = self.nodes[1].z_getbalance(nt1)
        nt2b = self.nodes[2].z_getbalance(nt2)

        print("N0 -> (" + nt0 + " , " + nt0b + ")")
        print("N0 -> (" + nz0 + " , " + nz0b + ")")
        print("N1 -> (" + nt1 + " , " + nt1b + ")")
        print("N2 -> (" + nt2 + " , " + nt2b + ")")

        return


        ## this fails due to joinsplit size
        #nz0 = self.nodes[0].z_listaddresses()[0]
        #nz0b = self.nodes[0].z_getbalance(nz0)
        #nt2 = self.nodes[2].listaddresses()[0]
        #nt2b = self.nodes[2].z_getbalance(nt2)
        #print(nz0b)
        #print(nt2b)
        #recipients= [{"address":nt2, "amount": nz0b}]
        #myopid = self.nodes[0].z_sendmany(nz0, recipients, 1, 0, True)
        #txid = wait_and_assert_operationid_status(self.nodes[0], myopid, "success", "", 3600)
        #self.sync_all()
        #self.nodes[1].generate(1)
        #self.sync_all()
        #nz0b = self.nodes[0].z_getbalance(nz0)
        #nt2b = self.nodes[2].z_getbalance(nt2)
        #print(nz0b)
        #print(nt2b)

        # # this fails due to vin/vout size
        # self.nodes[1].generate(1000)
        # self.sync_all()
        # nz0 = self.nodes[0].z_listaddresses()[0]
        # nz0b = self.nodes[0].z_getbalance(nz0)
        # nt1 = self.nodes[1].listaddressgroupings()[0][0][0]
        # nt1b = self.nodes[1].z_getbalance(nt1)
        # print(nz0b)
        # print(nt1b)
        # recipients= [{"address":nz0, "amount": nt1b}]
        # myopid = self.nodes[1].z_sendmany(nt1, recipients, 1, 0, True)
        # txid = wait_and_assert_operationid_status(self.nodes[1], myopid, "success", "", 3600)
        # self.sync_all()
        # self.nodes[1].generate(1)
        # self.sync_all()
        # nz0b = self.nodes[0].z_getbalance(nz0)
        # nt1b = self.nodes[1].z_getbalance(nt1)
        # print(nz0b)
        # print(nt1b)

        return

if __name__ == '__main__':
    ZSendmanyTest().main()