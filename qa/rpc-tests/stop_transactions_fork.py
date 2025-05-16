#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
import pprint
from decimal import Decimal

from test_framework.authproxy import JSONRPCException
from test_framework.mc_test.mc_test import *
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.util import initialize_chain_clean, \
    sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, assert_true, assert_false, start_node, assert_equal, \
    swap_bytes, get_epoch_data

DEBUG_MODE = 1
NUMB_OF_NODES = 3
EPOCH_LENGTH = 0
SC_VERSION = 2
SC_PARAMS_NAME = "sc"
CERT_FEE = Decimal('0.0001')
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')


class StopTransactions(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        for i in range(0, NUMB_OF_NODES):
            self.nodes += [
                start_node(i, self.options.tmpdir, extra_args=['-debug=py', '-debug=sc', '-logtimemicros=1'])]

        for k in range(0, NUMB_OF_NODES - 1):
            connect_nodes_bi(self.nodes, k, k + 1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def try_send_certificate(self, node_idx, scid, quality, bt):
        scid_swapped = str(swap_bytes(scid))
        epoch_number = self.nodes[node_idx].getscinfo(scid)['items'][0]['epoch']
        ref_height = self.nodes[node_idx].getblockcount() -1
        _, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[node_idx], 0, True, ref_height)
        if len(bt) == 0:
            pks = []
            amounts = []
            bt_list = []
        else:
            pks = [bt["address"]],
            amounts = [bt["amount"]]
            bt_list = [bt]

        proof = self.mc_test.create_test_proof(SC_PARAMS_NAME,
                                               scid_swapped,
                                               epoch_number,
                                               quality,
                                               MBTR_SC_FEE,
                                               FT_SC_FEE,
                                               epoch_cum_tree_hash,
                                               prev_cert_hash,
                                               constant = self.constant,
                                               pks = pks,
                                               amounts = amounts)


        mark_logs("Node {} sends cert of quality {} epoch {} ref {}".format(node_idx, quality, epoch_number, ref_height), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[node_idx].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, bt_list, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("Sent certificate {}".format(cert), self.nodes, DEBUG_MODE)
            return cert
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

            return None

    def run_test(self):

        """
        Test that after reaching the proper fork height, it is not possible to send any transaction. More in details:
        - Reach a chain height 1 block before the fork point
        - Create a new SC so that the tx goes into the mempool
        - Split the network:
           . network 0: a block is generated and the SC is created. The SC info can be retrieved
           . network 1: 2 blocks are generated so to get a longer chain
        - Join the network
           . The network 0 SC creation block is reverted, the relevant SC creation tx gets back to the mempool but is
             evicted since the fork point has been reached
           . The SC info is not retrievable anymore and no SC creation tx can be found in the mempool
        - Try creating a new SC and try to fwt to existing SC: it is not possible anymore after the fork point
        - Try other ways of sending funds via RPC commands and verify that they fail
        """

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(ForkHeights['NON_CEASING_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['NON_CEASING_SC'])
        self.sync_all()

        # sidechain creation
        # -------------------
        creation_amount = Decimal("1.0")
        # generate wCertVk and constant
        self.mc_test = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = self.mc_test.generate_params(SC_PARAMS_NAME, keyrot=True)
        self.constant = generate_random_field_element_hex()
        cmd_input = {
            "version": SC_VERSION,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": self.constant,
        }

        ret = self.nodes[0].sc_create(cmd_input)
        creating_tx = ret['txid']
        scid = ret['scid']
        self.sync_all()
        mark_logs("Node0 created SC scid={}, tx={}".format(scid, creating_tx), self.nodes, DEBUG_MODE)

        fwt_amount = Decimal("2.0")

        mark_logs("Node 1 sends " + str(fwt_amount) + " coins to SC", self.nodes, DEBUG_MODE)
        mc_return_address = self.nodes[1].getnewaddress()
        ft_cmd_input = [{'toaddress': "abcd", 'amount': fwt_amount, "scid": scid, 'mcReturnAddress': mc_return_address}]
        self.nodes[1].sc_send(ft_cmd_input)
        self.sync_all()

        current_height = self.nodes[0].getblockcount()
        mark_logs(("active chain height = %d: testing before sidechain fork" % current_height), self.nodes, DEBUG_MODE)

        # reach the height where the next block is the last before the fork point
        delta = ForkHeights['STOP_TRANSACTIONS'] - current_height - 2

        mark_logs("Node 1 generates {} blocks for reaching a pre-fork point, where no new SC creations "
                  "will be allowed as well as fw transfers to existing ones".format(delta), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(delta)
        self.sync_all()
        current_height = self.nodes[0].getblockcount()
        mark_logs(("active chain height = %d" % current_height), self.nodes, DEBUG_MODE)

        # crete raw txs useful for testing consensus rules after fork-point
        sc_cr = [{
            "version": 0,
            "epoch_length": 5,
            "amount": creation_amount,
            "address": "dada",
            "wCertVk": self.mc_test.generate_params("sc3"),
            "constant": generate_random_field_element_hex()
        }]
        raw_tx = self.nodes[1].createrawtransaction([], {}, [], sc_cr, [])
        funded_tx = self.nodes[1].fundrawtransaction(raw_tx)
        signed_sc_cr_tx = self.nodes[1].signrawtransaction(funded_tx['hex'])

        sc_ft = [{
            "address": "abc",
            "amount": fwt_amount,
            "scid": scid,
            "mcReturnAddress": mc_return_address
        }]
        raw_tx = self.nodes[1].createrawtransaction([], {}, [], [], sc_ft)
        funded_tx = self.nodes[1].fundrawtransaction(raw_tx)
        signed_sc_ft_tx = self.nodes[1].signrawtransaction(funded_tx['hex'])

        utx = self.nodes[0].listunspent()
        inputs = [{'txid': utx[0]['txid'], 'vout': utx[0]['vout']}]
        outputs = {self.nodes[1].getnewaddress(): utx[0]['amount']}
        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
        rawtx = self.nodes[0].signrawtransaction(rawtx)

        mark_logs("---------------- Split the network --------------", self.nodes, DEBUG_MODE)
        self.split_network(0)

        # ----------------------Network Part 0
        mark_logs("Node 0 creates SC2", self.nodes, DEBUG_MODE)
        cmd_input = {
            "version": 0,
            "withdrawalEpochLength": 5,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": self.mc_test.generate_params("sc2"),
            "constant": generate_random_field_element_hex()
        }
        ret = self.nodes[0].sc_create(cmd_input)
        creating_tx = ret['txid']

        mark_logs("Check creating tx={} is in node 0 mempool...".format(creating_tx), self.nodes, DEBUG_MODE)
        assert_true(creating_tx in self.nodes[0].getrawmempool())

        mark_logs("Node 0 generates 1 block", self.nodes, DEBUG_MODE)
        bl = self.nodes[0].generate(1)[0]
        pprint.pprint(self.nodes[0].getblock(bl))

        mark_logs("Check we do have the new SC in network part 0", self.nodes, DEBUG_MODE)
        sc_info_pre = self.nodes[0].getscinfo(ret['scid'])
        pprint.pprint(sc_info_pre)
        assert_true(1 == len(sc_info_pre['items']))

        # ----------------------Network Part 1
        mark_logs("Node 1 generates 2 blocks, crossing the fork point and reaching the longest chain", self.nodes,
                  DEBUG_MODE)
        self.nodes[1].generate(2)

        mark_logs("---------------------- Join the network -------------------", self.nodes, DEBUG_MODE)
        self.join_network(0)
        self.sync_all()

        sc_info_post = self.nodes[0].getscinfo(ret['scid'])

        mark_logs("Check we do not have the new SC in the joint network", self.nodes, DEBUG_MODE)
        pprint.pprint(sc_info_post)
        assert_true(0 == len(sc_info_post['items']))

        mark_logs("Check tx={} is not in mempool...".format(creating_tx), self.nodes, DEBUG_MODE)
        assert_false(creating_tx in self.nodes[0].getrawmempool())
        assert_false(creating_tx in self.nodes[1].getrawmempool())

        mark_logs("Node 1 tries to create a new SC, expecting error", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].sc_create(cmd_input)
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "This method is disabled"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        mark_logs("Node 1 tries to send " + str(fwt_amount) + " coins to SC1, expecting error", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].sc_send(ft_cmd_input)
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "This method is disabled"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        self.sync_all()

        mark_logs("Node 1 tries to create a new SC via a RPC raw tx, expecting error", self.nodes, DEBUG_MODE)
        sc_cr = [{
            "version": 0,
            "epoch_length": EPOCH_LENGTH,
            "amount": creation_amount,
            "address": "dada",
            "wCertVk": self.mc_test.generate_params("sc3"),
            "constant": generate_random_field_element_hex()
        }]

        try:
            self.nodes[1].createrawtransaction([], {}, [], sc_cr, [])
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "This method is disabled"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        mark_logs("Node 1 tries to send " + str(fwt_amount) + " coins to SC1 via RPC raw tx, expecting error",
                  self.nodes,
                  DEBUG_MODE)
        sc_ft = [{
            "address": "abc",
            "amount": fwt_amount,
            "scid": scid,
            "mcReturnAddress": mc_return_address
        }]

        try:
            self.nodes[1].createrawtransaction([], {}, [], [], sc_ft)
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "This method is disabled"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        self.sync_all()

        mark_logs("Node 1 tries to create a new SC via a sendraw tx, expecting error", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].sendrawtransaction(signed_sc_ft_tx['hex'])
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "16: bad-txs-stopped"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        mark_logs("Node 1 tries to send " + str(fwt_amount) + " coins to SC1 via sendraw tx, expecting error",
                  self.nodes,
                  DEBUG_MODE)
        try:
            self.nodes[1].sendrawtransaction(signed_sc_cr_tx['hex'])
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "16: bad-txs-stopped"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        # check we can not even send normal tx
        try:
            self.nodes[0].sendrawtransaction(rawtx['hex'])
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "16: bad-txs-stopped"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        # test sendmany() cmd
        outputs = {self.nodes[1].getnewaddress(): 1.1, self.nodes[1].getnewaddress(): 1.2,
                   self.nodes[1].getnewaddress(): 0.1, self.nodes[1].getnewaddress(): 1.3,
                   self.nodes[1].getnewaddress(): 0.2, self.nodes[1].getnewaddress(): 0.3}
        try:
            self.nodes[0].sendmany("", outputs)
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "This method is disabled"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        # test sendtoaddress() cmd
        try:
            self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 1.1)
        except JSONRPCException as e:
            error_string = e.error['message']
            print(error_string)
            expected_substring = "This method is disabled"
            assert expected_substring in error_string, f"'{error_string}' does not contain '{expected_substring}'"
        else:
            raise RuntimeError("An exception was expected")

        # mine some more blocks and verify that a new miner has spendable utxo after 100 blocks (coinbase txes are OK)
        block_hash = self.nodes[2].generate(1)[-1]
        self.sync_all()

        block_data = self.nodes[0].getblock(block_hash, 1)
        tx_data = block_data["tx"]
        pprint.pprint(tx_data[0])
        value = self.nodes[0].getrawtransaction(tx_data[0], 1)["vout"][0]['valueZat']

        self.nodes[2].generate(100)
        self.sync_all()
        utx_node2 = self.nodes[2].listunspent()
        pprint.pprint(utx_node2)
        assert_equal(utx_node2[0]['satoshis'], value)

        # SC's submitter node sends an empty certificate, verify we still receive it even if we have stopped all normal
        # transactions.
        cert_hash = self.try_send_certificate(node_idx=0, scid=scid, quality=2, bt={})
        self.sync_all()

        decoded_cert = self.nodes[1].getrawtransaction(cert_hash, 1)
        assert_equal(int(decoded_cert['cert']['totalAmount']), 0)

if __name__ == '__main__':
    StopTransactions().main()
