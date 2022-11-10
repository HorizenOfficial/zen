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

class TxFailBigDimension(BitcoinTestFramework):
    FEE = 0.0001

    def import_data_to_data_dir(self):
        
        # importing datadir resource
        #
        # (1 block generated on node0, 100 blocks generated on node1, coinbase shielding on node0) * 120 + 100 blocks generated on node2 +
        #
        # node0:
        # +] ztobEofNkizCXzVQoj9WaKJbbBehG2jEQ3a                                                             -> 0.00000000
        # +] ztgVYp9y6xG5iLtTkcJUhckoBtm1wQ8sedk                                                             -> 0.00000000
        # +] ztjkTZV9Aytqum7cg9rVgHoj4MbY99LX4NVh6wTXrwhQQjGHVYnEN7sTTimEQ8hz2N8V2bEhkBLDQSLQLkC41PdivcTc2QD -> 302.39843750 {120 notes}
        #
        # node1:
        # +] ztgdy7Z2Jp7PMjLcu1DEbRddyeRWdLV6TMJ                                                             -> 0.00000000
        # +] ztYrdPYew999PYvj5ESTHPuggvnMAaw46Sf                                                             -> 29762.03125000 {12000 utxos} [?11193?]
        #
        # node2:
        # +] ztZo6E8FwA3dprgf51pwyPX2xxVwCcYKvwb                                                             -> 0.00000000
		# +] ztV1ZjusV14v91xDK16F1mb8TTxtoD1BzTJ                                                             -> 0.00000000 (+11.71875000 immature) {100 utxos (+100 immature)}
        #

        resource_file = os.sep.join([os.path.dirname(__file__), 'resources', 'tx_fail_big_dimension', 'test_setup_.zip'])
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

        test_flow = 0 # sendtoaddress
        test_flow = 1 # sendfrom
        test_flow = 2 # sendmany
        test_flow = 3 # z_sendmany(z->t)
        test_flow = 4 # z_sendmany(t->z)
        test_flow = 5 # sc_send_certificate

        test_flow = 0
        print("test flow: " + str(test_flow))

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
            n1_listunspent = self.nodes[1].listunspent(0)
            n1_listunspent_sorted = sorted(n1_listunspent, key=lambda x: x["amount"], reverse=False) #because AsyncRPCOperation_sendmany::find_utxos performs an ascending sorting
            total_limited = 0
            for i in range(len(n1_listunspent_sorted)):
                #print(n1_listunspent_sorted[i]["amount"])
                total_limited += n1_listunspent_sorted[i]["amount"]
                if (i > 1000):
                    break
            print(total_limited)
            recipients= [{"address":nz0, "amount": total_limited}]
            myopid = self.nodes[1].z_sendmany(nt1, recipients, 1, 0, True)
            txid = wait_and_assert_operationid_status(self.nodes[1], myopid, "success", "", 3600)

        elif test_flow == 5:

            self.nodes[0].generate(110)
            self.sync_all()
            for i in range(1000):
                self.nodes[0].sendmany("", {nt1: 0.0000015}, 0)
            self.nodes[0].generate(10)
            self.sync_all()

            ## Create SC
            EPOCH_LENGTH = 4032
            FT_SC_FEE = Decimal('0')
            MBTR_SC_FEE = Decimal('0')
            CERT_FEE = Decimal('0.0015')
            creation_amount = Decimal("0.5")
            mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
            vk = mcTest.generate_params("sc1")
            constant = generate_random_field_element_hex()
            cmdInput = {
                "version": 0,
                "withdrawalEpochLength":EPOCH_LENGTH,
                "toaddress":"aaaa",
                "amount":creation_amount,
                "wCertVk":vk,
                "constant":constant,
                "fee":0
            }
            ret = self.nodes[1].sc_create(cmdInput)
            creating_tx = ret['txid']
            scid = ret['scid']
            scid_swapped = str(swap_bytes(scid))
            self.nodes[2].generate(EPOCH_LENGTH + 1) # node2 used in order to avoid confusion on balance for node1
            self.sync_all()
            scid = self.nodes[1].getscinfo("*")["items"][0]["scid"]
            scid_swapped = str(swap_bytes(scid))
            constant = self.nodes[1].getscinfo("*")["items"][0]["constant"]
            epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[1], EPOCH_LENGTH)
            quality = 1
            mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
            amount_cert = []
            proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [], [])
            cert = self.nodes[1].sc_send_certificate(scid, epoch_number, quality, epoch_cum_tree_hash, proof, amount_cert, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            return

        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()

        nt0b = self.nodes[0].z_getbalance(nt0)
        nz0b = self.nodes[0].z_getbalance(nz0)
        nt1b = self.nodes[1].z_getbalance(nt1)
        nt2b = self.nodes[2].z_getbalance(nt2)

        print("N0 -> (" + nt0 + " , " + str(nt0b) + ")")
        print("N0 -> (" + nz0 + " , " + str(nz0b) + ")")
        print("N1 -> (" + nt1 + " , " + str(nt1b) + ")")
        print("N2 -> (" + nt2 + " , " + str(nt2b) + ")")

        return

if __name__ == '__main__':
    TxFailBigDimension().main()

#TEMP
#            creation_amount = Decimal("0.5")
#            mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
#            vk = mcTest.generate_params("sc1")
#            constant = generate_random_field_element_hex()
#            cmdInput = {
#                "version": 0,
#                "withdrawalEpochLength":EPOCH_LENGTH,
#                "toaddress":"aaaa",
#                "amount":creation_amount,
#                "wCertVk":vk,
#                "constant":constant,
#                "fee":0
#            }
#            ret = self.nodes[1].sc_create(cmdInput)
#            creating_tx = ret['txid']
#            scid = ret['scid']
#            scid_swapped = str(swap_bytes(scid))
#            self.nodes[2].generate(EPOCH_LENGTH + 1) # node2 used in order to avoid confusion on balance for node1
#            self.sync_all()
###            # Send funds to SC (in many iterations, in order to create many utxos)
#            n1_listunspent = self.nodes[1].listunspent(0)
#            n1_listunspent_sorted = sorted(n1_listunspent, key=lambda x: x["amount"], reverse=False) #because AsyncRPCOperation_sendmany::find_utxos performs an ascending sorting
#            tot_to_sc = 0
#            for i in range(len(n1_listunspent_sorted)):
#                fwt_amount = n1_listunspent_sorted[i]["amount"]
#                tot_to_sc += fwt_amount
#                #print(str(i) + ": " + str(fwt_amount))
#                cmdInput = [{"toaddress": "bbb", "amount": fwt_amount, "scid": scid, "mcReturnAddress": nt1}]
#                cmdParam = { "fromaddress": nt1, "changeaddress": nt1, "minconf": 0, "fee": 0 }
#                fwd_tx = self.nodes[1].sc_send(cmdInput, cmdParam)
#                self.sync_all()
#                if (i % 1 == 1 - 1):
#                    self.nodes[2].generate(1) # node2 used in order to avoid confusion on balance for node1
#                    self.sync_all()
#                    #self.nodes[1].getscinfo(scid)['items'][0]['balance']
#                if (i >= EPOCH_LENGTH / 5 - 1 - 1 - 1):
#                    break
#            self.sync_all()
#            print(tot_to_sc)
#            #return