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
        # +] ztobEofNkizCXzVQoj9WaKJbbBehG2jEQ3a                                                             -> 0.70000000 {700 utxos}
        # +] ztgdy7Z2Jp7PMjLcu1DEbRddyeRWdLV6TMJ                                                             -> 0.00000000
        # +] ztgVYp9y6xG5iLtTkcJUhckoBtm1wQ8sedk                                                             -> 0.00000000
        # +] ztjkTZV9Aytqum7cg9rVgHoj4MbY99LX4NVh6wTXrwhQQjGHVYnEN7sTTimEQ8hz2N8V2bEhkBLDQSLQLkC41PdivcTc2QD -> 302.39843750 {120 notes}
        #
        # node1:
        # +] ztgdy7Z2Jp7PMjLcu1DEbRddyeRWdLV6TMJ \                                                           -> \
        # +] ztTEh4stUFTat2e9JbmVkfx9vqGhS2yA4a9  \                                                          ->  \
        # ...                                      - 700                                                          tot 149.18171725
        # +] ztdn7RV49ZrjKQBqXsC2twd9uWaV5YE51Pv  /                                                          -> /
        # +] ztYrdPYew999PYvj5ESTHPuggvnMAaw46Sf /                                                           -> 29612.14743451 {12000 utxos} [?11193?]
        #
        # node2:
        # +] ztZo6E8FwA3dprgf51pwyPX2xxVwCcYKvwb                                                             -> 0.00000000
		# +] ztV1ZjusV14v91xDK16F1mb8TTxtoD1BzTJ                                                             -> 0.00000000 (+11.71875000 immature) {100 utxos (+100 immature)}
        #

        #resource_file = os.sep.join([os.path.dirname(__file__), 'resources', 'tx_fail_big_dimension', 'test_setup_.zip'])
        resource_file = os.sep.join([os.path.dirname(__file__), 'resources', 'tx_fail_big_dimension_new2', 'test_setup_.zip'])
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
        test_flow = 5 # z_sendmany(t->t)
        test_flow = 6 # sc_send_certificate

        test_flow = 3
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

        if test_flow == -1:
            #on nt0 we already have 700 coins of size 0.001
            self.nodes[1].sendtoaddress(nt0, 0.5)
            self.sync_all()
            self.nodes[2].generate(10)
            self.sync_all()
            self.nodes[0].sendtoaddress(nt2, 0.550) #fail #here we see that old algo selects 51 utxos (with 0 change) while new algo selects 550 utxos (with 0 change)
            # self.nodes[0].sendtoaddress(nt2, 0.700) #fail
            # self.nodes[0].sendtoaddress(nt2, 0.710) #pass
            self.sync_all()
            self.nodes[2].generate(10)
            self.sync_all()

        if test_flow == 0:
            #on nt0 we already have 700 coins of size 0.001
            self.nodes[1].sendtoaddress(nt0, 0.7)
            self.sync_all()
            self.nodes[2].generate(10)
            self.sync_all()
            self.nodes[0].sendtoaddress(nt2, 0.680) #fail
            # self.nodes[0].sendtoaddress(nt2, 0.700) #fail
            # self.nodes[0].sendtoaddress(nt2, 0.710) #pass
            self.sync_all()
            self.nodes[2].generate(10)
            self.sync_all()
        
        elif test_flow == 1:
            self.nodes[1].sendfrom("", nt0, nt1b)

        elif test_flow == 2:
            self.nodes[1].sendmany("", {nt0: nt1b}, 0, "", [])

        elif test_flow == 3:
            # from Z to T
            fromZ = True
            toZ = True
            mix = False
            fromAddr = nz0 if fromZ else nt0
            for round in range(1, 8):
            #for round in range(6, 8):
                recipients = []
                listunspent = sorted(self.nodes[0].z_listunspent(0) if fromZ else self.nodes[0].listunspent(0), key=lambda x: x["amount"], reverse=True)
                for i in range(10):
                    print(listunspent[i]["amount"])
                print("...")
                print(listunspent[len(listunspent) - 1]["amount"])
                for size in range(round):
                    to = self.nodes[2].z_getnewaddress() if toZ else self.nodes[2].getnewaddress()
                    amount = 2.5 if fromZ else 0.005
                    if (mix):
                        if (size % 2 == 0):
                            to = self.nodes[2].z_getnewaddress()
                            amount += 0.0
                        else:
                            to = self.nodes[2].getnewaddress()
                            amount -= 0.001
                    recipients.append({"address": to, "amount": amount})
                myopid = self.nodes[0].z_sendmany(fromAddr, recipients, 1, 0, True)
                txid = wait_and_assert_operationid_status(self.nodes[0], myopid, "success", "", 3600)
                self.sync_all()
                self.nodes[2].generate(1)
                self.sync_all()
                hex = self.nodes[0].gettransaction(txid)['hex']
                dhex = self.nodes[0].decoderawtransaction(hex)
                print("vout      : " + str(len(dhex["vout"])))
                print("vin       : " + str(len(dhex["vin"])))
                print("vjoinsplit: " + str(len(dhex["vjoinsplit"])))
                print("")
            # from Z to T
            # from T to Z

            var = 0


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
            n1_listunspent = self.nodes[1].listunspent(0)
            n1_listunspent_sorted = sorted(n1_listunspent, key=lambda x: x["amount"], reverse=False) #because AsyncRPCOperation_sendmany::find_utxos performs an ascending sorting
            total_limited = 0
            for i in range(len(n1_listunspent_sorted)):
                #print(n1_listunspent_sorted[i]["amount"])
                total_limited += n1_listunspent_sorted[i]["amount"]
                if (i > 1000):
                    break
            print(total_limited)
            recipients= [{"address":nt0, "amount": total_limited}]
            myopid = self.nodes[1].z_sendmany(nt1, recipients, 1, 0, True)
            txid = wait_and_assert_operationid_status(self.nodes[1], myopid, "success", "", 3600)

        elif test_flow == 6:

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