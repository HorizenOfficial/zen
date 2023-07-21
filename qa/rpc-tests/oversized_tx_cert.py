#!/usr/bin/env python3
import codecs
from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_greater_or_equal_than, initialize_chain_clean,\
    get_epoch_data, start_nodes, connect_nodes_bi, stop_node, wait_and_assert_operationid_status, swap_bytes
import os
import zipfile
import time
from test_framework.test_framework import MINER_REWARD_POST_H200
from test_framework.mc_test.mc_test import *

EPS = Decimal(0.00000001)

class OversizedTxCert(BitcoinTestFramework):

    def import_data_to_data_dir(self):

        # importing datadir resource
        #
        # 101 blocks generated on node2 + (0.001 sent from node2 to node0) * 2000 +
        # 1 block generated on node2 + 200 blocks generated on node1 +
        # (single shielding on node1 coinbase + 1 block generated on node2) * 200
        #
        # node0:
        # +] ztoEcvcprfPWbfzYXUAFaA12BFCck9hXVYw                                                             -> 2.00000000 {2000 utxos}
        #
        # node1:
        # +] ztjUrw6vnos7grPAFHh98w1Qct2LjX3zxE2                                                             -> 0.00000000
        # +] ztni5Ykn2RCkojLQX1QdDR4iEGYnBYaH7HMNXo12xYoy5isQfBTLpNeoEjm6LhgeCtbNhCtNcrDpqp8ReR1cwjqadrmhdxV -> 1625.73000000 {200 utxos}
        #
        # node2:
        # +] zta36sLgzD3H81nWXkv3qfLnSBXbXsrg1oW                                                             -> 0.00000000
        # +] ztr1k6dK5D3XqizVSBhekFMx45NdWeo3gbJ                                                             -> 761.01592000 (+750.01000000 immature) {101 utxos (+100 immature)}
        # +] ztoap92Ej7A13F3uhBbT81QEvzVNkTgwC6Q                                                             -> 1143.31225000 {101 utxos}
        # +] ztfUjF8wQfJkzuJYAMSMVgamKGNEacUozb5                                                             -> 9.43158000 {1 utxos}
        #

        resource_file = os.sep.join([os.path.dirname(__file__), 'resources', 'oversized_tx_cert', 'test_setup_.zip'])
        with zipfile.ZipFile(resource_file, 'r') as zip_ref:
            zip_ref.extractall(self.options.tmpdir)

    def setup_chain(self):
        self.import_data_to_data_dir()
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_network(self, split=False):
        self.nodes = start_nodes(3, self.options.tmpdir, extra_args=[['-debug=zrpcunsafe', '-debug=selectcoins', '-maxtipage=36000000']] * 3 )
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
        nz1 = self.nodes[1].z_listaddresses()[0]
        nt2 = self.nodes[2].listaddresses()[0]

        nt0b = self.nodes[0].z_getbalance(nt0)
        nt1b = self.nodes[1].z_getbalance(nt1)
        nz1b = self.nodes[1].z_getbalance(nz1)
        nt2b = self.nodes[2].z_getbalance(nt2)


        # ---------- testing sendtoaddress (sendfrom is analogous) ----------
        try:
            # not enough funds for transaction
            self.nodes[0].sendtoaddress(nt2, nt0b + EPS)
            assert_equal(False) # impossibile to create transaction (as expected)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Impossible to create transaction (as expected): {}".format(errorString))

        try:
            # no middle value coin is sent, hence it will be impossible (due to oversize) to create next transaction
            # using only small coins
            self.nodes[0].sendtoaddress(nt2, 0.7)
            assert_equal(False) # impossibile to create transaction (as expected)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Impossible to create transaction (as expected): {}".format(errorString))

        # a middle value coin is sent which will be later used in next transaction (together with other coins)
        # no oversized transaction error is expected ("JSONRPC error: Transaction too large")
        self.nodes[2].sendtoaddress(nt0, 0.5)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        listunspent0 = self.nodes[0].listunspent(0, 9999999, [nt0])
        assert_equal(len(list(filter(lambda x : abs(x["amount"] - Decimal(0.5)) < EPS, listunspent0))), 1) # coin unspent
        self.nodes[0].sendtoaddress(nt2, 0.7)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        listunspent0 = self.nodes[0].listunspent(0, 9999999, [nt0])
        assert_equal(len(list(filter(lambda x : abs(x["amount"] - Decimal(0.5)) < EPS, listunspent0))), 0) # coin spent

        # a big value coin is sent which will be later used in next transaction as the only coin (possibly with few
        # more due to change levels) because it would be impossible (due to oversize) to create next transaction using
        # only small coins
        self.nodes[2].sendtoaddress(nt0, 1.0)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        listunspent0 = self.nodes[0].listunspent(0, 9999999, [nt0])
        assert_equal(len(list(filter(lambda x : abs(x["amount"] - Decimal(1.0)) < EPS, listunspent0))), 1) # coin unspent
        txid = self.nodes[0].sendtoaddress(nt2, 1.0)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        listunspent0 = self.nodes[0].listunspent(0, 9999999, [nt0])
        assert_equal(len(list(filter(lambda x : abs(x["amount"] - Decimal(1.0)) < EPS, listunspent0))), 0) # coin spent
        hex = self.nodes[0].gettransaction(txid)['hex']
        vins = self.nodes[0].decoderawtransaction(hex)['vin']
        assert_greater_or_equal_than(len(vins), 1)

        # a big value coin is sent which will be later used in next transaction as the only coin because it would be
        # impossible (due to oversize) to create next transaction using only small coins; this time an exact match can
        # be found because coin gross value is used (instead of net value)
        self.nodes[2].sendtoaddress(nt0, 1.0)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        listunspent0 = self.nodes[0].listunspent(0, 9999999, [nt0])
        assert_equal(len(list(filter(lambda x : abs(x["amount"] - Decimal(1.0)) < EPS, listunspent0))), 1) # coin unspent
        txid = self.nodes[0].sendtoaddress(nt2, 1.0, "", "", True)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        listunspent0 = self.nodes[0].listunspent(0, 9999999, [nt0])
        assert_equal(len(list(filter(lambda x : abs(x["amount"] - Decimal(1.0)) < EPS, listunspent0))), 0) # coin spent
        hex = self.nodes[0].gettransaction(txid)['hex']
        vins = self.nodes[0].decoderawtransaction(hex)['vin']
        assert_equal(len(vins), 1)


        # ---------- testing sendmany ----------
        # two middle value coins are sent but will not suffice (due to oversize) for creating next transaction (even if
        # mixed with other small coins)
        self.nodes[2].sendtoaddress(nt0, 0.5)
        self.sync_all()
        self.nodes[2].sendtoaddress(nt0, 0.5)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        listunspent0 = self.nodes[0].listunspent(0)
        assert_equal(len(list(filter(lambda x : abs(x["amount"] - Decimal(0.5)) < EPS, listunspent0))), 2) # coins unspent
        try:
            self.nodes[0].sendmany("", {nt2: 1.0, self.nodes[2].getnewaddress(): 1.0}, 0, "", [])
            assert_equal(False) # impossibile to create transaction (as expected)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Impossible to create transaction (as expected): {}".format(errorString))

        # two middle value coins previously sent are selected (together with other small coins) for creating next transaction
        self.nodes[2].generate(1)
        self.sync_all()
        listunspent0 = self.nodes[0].listunspent(0)
        assert_equal(len(list(filter(lambda x : abs(x["amount"] - Decimal(0.5)) < EPS, listunspent0))), 2) # coins unspent
        self.nodes[0].sendmany("", {nt2: 0.7, self.nodes[2].getnewaddress(): 0.7}, 0, "", [])
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        listunspent0 = self.nodes[0].listunspent(0)
        assert_equal(len(list(filter(lambda x : abs(x["amount"] - Decimal(0.5)) < EPS, listunspent0))), 0) # coins spent
        

        # ---------- testing sc_create and sc_send_certificate ----------
        # emptying node0 wallet (with some txs, not in a single tx otherwise we would incur in oversized tx)
        partialBalance0 = 0
        listunspent0 = self.nodes[0].listunspent(0)
        while(len(listunspent0) > 0):
            partialBalance0 = sum(item['amount'] for item in listunspent0[:600]) # an "almost void" transaction supports approximately up to 650 inputs
            self.nodes[0].sendtoaddress(nt2, partialBalance0, "", "", True)
            self.sync_all()
            self.nodes[2].generate(1)
            self.sync_all()
            listunspent0 = self.nodes[0].listunspent(0)

        # sending a lot of small coins to node0 wallet (syncing only at loop end, since only node2 is involved)
        for i in range(1100): # an "almost void" certificate supports approximately up to 1000 inputs
            self.nodes[2].sendtoaddress(nt0, 0.000005)
            if ((i + 1) % 600 == 0 or i == 1100 - 1):
                self.nodes[2].generate(1)
        self.sync_all()
        
        EPOCH_LENGTH = 10
        FT_SC_FEE = Decimal('0')
        MBTR_SC_FEE = Decimal('0')
        CERT_FEE = Decimal('0.005400')
        creation_amount = Decimal("0.00005")
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc0")
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

        # only the small coins will be selected for satisfying sc creation amount
        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        self.sync_all()
        self.nodes[2].generate(EPOCH_LENGTH)
        self.sync_all()
        scid = self.nodes[0].getscinfo("*")["items"][0]["scid"]
        scid_swapped = str(swap_bytes(scid))
        constant = self.nodes[0].getscinfo("*")["items"][0]["constant"]
        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        quality = 1
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        amount_cert = []
        proof = mcTest.create_test_proof("sc0", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, None, constant, [], [])
        
        try:
            # using only small coins results in an oversized certificate
            cert = self.nodes[0].sc_send_certificate(scid, epoch_number, quality, epoch_cum_tree_hash, proof, amount_cert, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert_equal(False) # impossibile to create certificate (as expected)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Impossible to create certificate (as expected): {}".format(errorString))

        # sending one big coin to node0 wallet
        self.nodes[2].sendtoaddress(nt0, 0.005445)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        # the big coin together with small coins will be selected for satisfying certificate fee
        listunspent0 = self.nodes[0].listunspent(0)
        assert_equal(len(list(filter(lambda x : abs(x["amount"] - Decimal(0.005445)) < EPS, listunspent0))), 1) # coin unspent
        cert = self.nodes[0].sc_send_certificate(scid, epoch_number, quality, epoch_cum_tree_hash, proof, amount_cert, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        listunspent0 = self.nodes[0].listunspent(0)
        assert_equal(len(list(filter(lambda x : abs(x["amount"] - Decimal(0.005445)) < EPS, listunspent0))), 0) # coin spent


        # ---------- testing z_sendmany (from_t part) ----------
        # failure due to too many transparents inputs selected even if single recipient
        errorCode = 0
        trecipients = []
        trecipients.append({"address":self.nodes[0].getnewaddress(), "amount": 0.000005 * 700})
        myopid = self.nodes[0].z_sendmany(nt0, trecipients)
        result = wait_and_assert_operationid_status(self.nodes[0], myopid, "failed", "")
        errorCode = result["error"]["code"]
        print("Impossible to create transaction (as expected): {}".format(result["error"]["message"]))
        assert_equal(errorCode, -6)

        # failure due to too many transparent inputs and transparent outputs selected
        errorCode = 0
        trecipients = []
        for i in range (600):
            trecipients.append({"address":self.nodes[0].getnewaddress(), "amount": 0.000005})
        myopid = self.nodes[0].z_sendmany(nt0, trecipients)
        result = wait_and_assert_operationid_status(self.nodes[0], myopid, "failed", "")
        errorCode = result["error"]["code"]
        print("Impossible to create transaction (as expected): {}".format(result["error"]["message"]))
        assert_equal(errorCode, -6)
        # sending one big coin to node0 wallet (should satisfy approximately 500 over 600 recipients)
        self.nodes[2].sendtoaddress(nt0, 0.0025)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        # the big coin together with small coins will be selected
        listunspent0 = self.nodes[0].listunspent(0, 9999999, [nt0])
        assert_equal(len(list(filter(lambda x : abs(x["amount"] - Decimal(0.0025)) < EPS, listunspent0))), 1) # coin unspent
        myopid = self.nodes[0].z_sendmany(nt0, trecipients)
        wait_and_assert_operationid_status(self.nodes[0], myopid)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        listunspent0 = self.nodes[0].listunspent(0, 9999999, [nt0])
        assert_equal(len(list(filter(lambda x : abs(x["amount"] - Decimal(0.0025)) < EPS, listunspent0))), 0) # coin spent

        # failure due to too many transparent inputs and shielded outputs selected
        errorCode = 0
        zrecipients = []
        for i in range (100):
            zrecipients.append({"address":self.nodes[0].z_getnewaddress(), "amount": 0.000005})
        try:
            myopid = self.nodes[0].z_sendmany(nt0, zrecipients)
            assert_equal(False)
        except JSONRPCException as e:
            errorCode = e.error["code"]
            errorString = e.error['message']
            assert_equal(-8, errorCode)
            print("Impossible to create transaction (as expected): {}".format(errorString))
        # reducing the number of recipients allows the transaction to be created
        errorCode = 0
        zrecipients = []
        for i in range (10):
            zrecipients.append({"address":self.nodes[0].z_getnewaddress(), "amount": 0.000005})
        myopid = self.nodes[0].z_sendmany(nt0, zrecipients)
        result = wait_and_assert_operationid_status(self.nodes[0], myopid, timeout=600) # longer timeout because 5=10/2 joinsplits have to be performed
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()


        # ---------- testing z_sendmany (from_z part) ----------
        # failure due to notes selection resulting in too many joinsplits (as input) even if single recipient
        z_listunspent_1 = self.nodes[1].z_listunspent(0, 9999999, False, [nz1])
        errorCode = 0
        zrecipients = []
        zrecipients.append({"address":self.nodes[1].z_getnewaddress(), "amount": self.nodes[1].z_getbalance(nz1)})
        myopid = self.nodes[1].z_sendmany(nz1, zrecipients, 1, 0)
        result = wait_and_assert_operationid_status(self.nodes[1], myopid, "failed", "")
        errorCode = result["error"]["code"]
        print("Impossible to create transaction (as expected): {}".format(result["error"]["message"]))
        assert_equal(errorCode, -6)

        # but reducing single recipient amount, less notes are selected and transaction can be created
        if (len(z_listunspent_1) > 5):
            z_listunspent_1_asc = sorted(z_listunspent_1, key=lambda x: x["amount"], reverse=False)
            zrecipients = []
            zrecipients.append({"address":self.nodes[1].z_getnewaddress(), "amount": z_listunspent_1_asc[0]["amount"] +
                                                                                     z_listunspent_1_asc[1]["amount"] +
                                                                                     z_listunspent_1_asc[2]["amount"] +
                                                                                     z_listunspent_1_asc[3]["amount"] +
                                                                                     z_listunspent_1_asc[4]["amount"]})
            myopid = self.nodes[1].z_sendmany(nz1, zrecipients, 1, 0)
            self.sync_all()
            self.nodes[2].generate(1)
            self.sync_all()
            txid = wait_and_assert_operationid_status(self.nodes[1], myopid)
            self.sync_all()
            self.nodes[2].generate(1)
            self.sync_all()
            hex = self.nodes[1].gettransaction(txid)['hex']
            vjoinsplit = self.nodes[1].decoderawtransaction(hex)['vjoinsplit']
            assert_equal(len(vjoinsplit), 3) # joinsplit inputs chaining: 2 -> +2 (no change) -> +1

        # try to send minimum amount and check that selection is optimal (trivial quantity maximization, change minimization in case of ties)
        z_listunspent_1 = self.nodes[1].z_listunspent(0, 9999999, False, [nz1])
        z_listunspent_1_asc = sorted(z_listunspent_1, key=lambda x: x["amount"], reverse=False)
        z_listunspent_1_min = z_listunspent_1_asc[0]["amount"]
        z_listunspent_1_max = z_listunspent_1_asc[len(z_listunspent_1_asc) - 1]["amount"]
        z_listunspent_1_min_count = len(list(filter(lambda x : abs(x["amount"] - Decimal(z_listunspent_1_min)) < EPS, z_listunspent_1)))
        z_listunspent_1_max_count = len(list(filter(lambda x : abs(x["amount"] - Decimal(z_listunspent_1_max)) < EPS, z_listunspent_1)))
        zrecipients = []
        zrecipients.append({"address":self.nodes[1].z_getnewaddress(), "amount": z_listunspent_1_min})
        myopid = self.nodes[1].z_sendmany(nz1, zrecipients, 1, 0)
        wait_and_assert_operationid_status(self.nodes[1], myopid)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        z_listunspent_1_pre = z_listunspent_1
        z_listunspent_1 = self.nodes[1].z_listunspent(0, 9999999, False, [nz1])
        # this shows exactly one note was used
        assert_equal(len(z_listunspent_1_pre) - 1, len(z_listunspent_1))
        z_listunspent_1_asc = sorted(z_listunspent_1, key=lambda x: x["amount"], reverse=False)
        # this shows the smallest note was used (in order to minimize change); in previous implementation biggest note was always used (resulting in high change)
        assert_equal(z_listunspent_1_min_count - 1, len(list(filter(lambda x : abs(x["amount"] - Decimal(z_listunspent_1_min)) < EPS, z_listunspent_1))))
        assert_equal(z_listunspent_1_max_count, len(list(filter(lambda x : abs(x["amount"] - Decimal(z_listunspent_1_max)) < EPS, z_listunspent_1))))

if __name__ == '__main__':
        OversizedTxCert().main()