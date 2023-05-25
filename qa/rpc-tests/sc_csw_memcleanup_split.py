#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test that a CSW transaction gets removed from mempool in case a sidechain
# passes from state "CEASED" to "ALIVE" (due to a blockchain fork and the
# revert of some blocks).
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, connect_nodes_bi, assert_true, assert_false, mark_logs, \
    get_epoch_data, sync_mempools, sync_blocks, \
    disconnect_nodes, advance_epoch, swap_bytes

from test_framework.test_framework import ForkHeights
from test_framework.mc_test.mc_test import CertTestUtils, CSWTestUtils, generate_random_field_element_hex

from decimal import Decimal
import pprint

NUMB_OF_NODES = 4
DEBUG_MODE = 1
EPOCH_LENGTH = 10
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')

DISHONEST_NODE_INDEX = NUMB_OF_NODES - 1    # Dishonest node
HONEST_NODES = list(range(NUMB_OF_NODES))
HONEST_NODES.remove(DISHONEST_NODE_INDEX)
MAIN_NODE = HONEST_NODES[0]                 # Hardcoded alias, do not change

# Create one-input, one-output, no-fee transaction:
class CertMempoolCleanupSplit(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self):
        assert_equal(MAIN_NODE, HONEST_NODES[0])

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[['-logtimemicros=1', '-scproofqueuesize=0', '-debug=sc', '-debug=py',
                                              '-debug=mempool', '-debug=net', '-debug=bench']] * NUMB_OF_NODES)

        for idx in range(NUMB_OF_NODES - 1):
            connect_nodes_bi(self.nodes, idx, idx + 1)
        self.is_network_split = False
        self.sync_all()

    def split_network(self):
        # Disconnect the dishonest node from the network
        idx = DISHONEST_NODE_INDEX
        if idx == NUMB_OF_NODES - 1:
            disconnect_nodes(self.nodes[idx], idx - 1)
            disconnect_nodes(self.nodes[idx - 1], idx)
        elif idx == 0:
            disconnect_nodes(self.nodes[idx], idx + 1)
            disconnect_nodes(self.nodes[idx + 1], idx)
        else:
            disconnect_nodes(self.nodes[idx], idx - 1)
            disconnect_nodes(self.nodes[idx - 1], idx)
            disconnect_nodes(self.nodes[idx], idx + 1)
            disconnect_nodes(self.nodes[idx + 1], idx)
            connect_nodes_bi(self.nodes, idx - 1, idx + 1)
        self.is_network_split = True

    def join_network(self):
        #Join the (previously split) network pieces together
        idx = DISHONEST_NODE_INDEX
        if idx == NUMB_OF_NODES - 1:
            connect_nodes_bi(self.nodes, idx, idx - 1)
        elif idx == 0:
            connect_nodes_bi(self.nodes, idx, idx + 1)
        else:
            disconnect_nodes(self.nodes[idx - 1], idx + 1)
            disconnect_nodes(self.nodes[idx + 1], idx - 1)
            connect_nodes_bi(self.nodes, idx, idx - 1)
            connect_nodes_bi(self.nodes, idx, idx + 1)
        self.is_network_split = False

    def run_test(self):
        '''
        Create a SC, advance two epochs, move to the limit of the safe guard and then split the network. 
        One network part sends a certificate to keep the sidechain alive and then generates two blocks.
        The other network part generates one block, theny sends a CSW.
        When the network is joined, verify that the SC is alive and that the CSW transaction has been
        removed from mempool.
        '''

        # prepare some coins 
        self.nodes[DISHONEST_NODE_INDEX].generate(1)
        self.sync_all()
        self.nodes[HONEST_NODES[2]].generate(1)
        self.sync_all()
        self.nodes[HONEST_NODES[1]].generate(1)
        self.sync_all()
        self.nodes[MAIN_NODE].generate(ForkHeights['MINIMAL_SC']-3)
        self.sync_all()
        self.nodes[MAIN_NODE].generate(1)
        self.sync_all()

        print(f"Node {MAIN_NODE} Chain h = {self.nodes[MAIN_NODE].getblockcount()}")

        sc_address = "0000000000000000000000000000000000000000000000000000000000000abc"
        sc_epoch_len = EPOCH_LENGTH
        sc_cr_amount = Decimal('12.00000000')

        certMcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        cswMcTest = CSWTestUtils(self.options.tmpdir, self.options.srcdir)

        # generate wCertVk and constant
        vk = certMcTest.generate_params("sc1")
        cswVk = cswMcTest.generate_params("csw1")
        constant = generate_random_field_element_hex()

        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': sc_epoch_len,
            'amount': sc_cr_amount,
            'toaddress': sc_address,
            'wCertVk': vk,
            'wCeasedVk': cswVk,
            'constant': constant,
            'mainchainBackwardTransferRequestDataLength': 1
        }

        res = self.nodes[MAIN_NODE].sc_create(cmdInput)
        tx =   res['txid']
        scid = res['scid']
        self.sync_all()
        mark_logs(f"tx {tx} created SC {scid}", self.nodes, DEBUG_MODE)

        # advance two epochs
        mark_logs("\nLet 2 epochs pass by...", self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[MAIN_NODE], self.sync_all,
             scid, "sc1", constant, sc_epoch_len)

        mark_logs(f"\n==> certificate for epoch {epoch_number} {cert}", self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[MAIN_NODE], self.sync_all,
             scid, "sc1", constant, sc_epoch_len)

        mark_logs(f"\n==> certificate for epoch {epoch_number} {cert}", self.nodes, DEBUG_MODE)

        ceas_height = self.nodes[MAIN_NODE].getscinfo(scid, False, False)['items'][0]['ceasingHeight']
        numbBlocks = ceas_height - self.nodes[MAIN_NODE].getblockcount() + sc_epoch_len - 1
        print(f"Node {MAIN_NODE} Chain h = {self.nodes[MAIN_NODE].getblockcount()}")

        mark_logs(f"\nNode {MAIN_NODE} generates {numbBlocks} block reaching the sg for the next epoch", self.nodes, DEBUG_MODE)
        self.nodes[MAIN_NODE].generate(numbBlocks)
        self.sync_all()
        print(f"Node {MAIN_NODE} Chain h = {self.nodes[MAIN_NODE].getblockcount()}")

        #============================================================================================
        mark_logs("\nSplit network", self.nodes, DEBUG_MODE)
        self.split_network()
        mark_logs(f"The network is split: {HONEST_NODES[0]}-{HONEST_NODES[1]}-{HONEST_NODES[2]} .. {DISHONEST_NODE_INDEX}", self.nodes, DEBUG_MODE)

        # Honest network
        print("------------------")

        mark_logs(f"\nNTW part 1) Node {HONEST_NODES[2]} sends a certificate", self.nodes, DEBUG_MODE)
        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[HONEST_NODES[2]], sc_epoch_len)

        bt_amount = Decimal("5.0")
        addr_node1 = self.nodes[HONEST_NODES[1]].getnewaddress()
        quality = 10
        scid_swapped = str(swap_bytes(scid))

        proof = certMcTest.create_test_proof("sc1",
                                             scid_swapped,
                                             epoch_number,
                                             quality,
                                             MBTR_SC_FEE,
                                             FT_SC_FEE,
                                             epoch_cum_tree_hash,
                                             prev_cert_hash = None,
                                             constant       = constant,
                                             pks            = [addr_node1],
                                             amounts        = [bt_amount])

        amount_cert = [{"address": addr_node1, "amount": bt_amount}]
        try:
            cert_bad = self.nodes[HONEST_NODES[2]].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert, 0, 0, 0.01)
        except JSONRPCException as e:
            errorString = e.error['message']
            print(f"Send certificate failed with reason {errorString}")
            assert(False)
        sync_mempools([self.nodes[i] for i in HONEST_NODES])

        mark_logs(f"Check cert {cert_bad} is in mempool", self.nodes, DEBUG_MODE)
        assert_true(cert_bad in self.nodes[MAIN_NODE].getrawmempool())

        mark_logs("Generates two blocks to make the chain longer than sub-network 2", self.nodes, DEBUG_MODE)
        self.nodes[MAIN_NODE].generate(2)

        print(f"Node {MAIN_NODE} Chain h = {self.nodes[MAIN_NODE].getblockcount()}")

        # Network part 2
        #------------------

        mark_logs(f"\nNTW part 2) Dishonest Node {DISHONEST_NODE_INDEX} generates one block to cease the sidechain", self.nodes, DEBUG_MODE)
        self.nodes[DISHONEST_NODE_INDEX].generate(1)

        mark_logs("Check that the sidechain is ceased from node 3 perspective", self.nodes, DEBUG_MODE)
        ret = self.nodes[DISHONEST_NODE_INDEX].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")

        sc_bal = ret['balance']

        mark_logs(f"Dishonest node {DISHONEST_NODE_INDEX} creates a CSW transaction", self.nodes, DEBUG_MODE)

        csw_mc_address = self.nodes[DISHONEST_NODE_INDEX].getnewaddress()
        taddr = self.nodes[DISHONEST_NODE_INDEX].getnewaddress()
        sc_csw_amount = sc_bal
        null = generate_random_field_element_hex()
        actCertData            = self.nodes[DISHONEST_NODE_INDEX].getactivecertdatahash(scid)['certDataHash']
        ceasingCumScTxCommTree = self.nodes[DISHONEST_NODE_INDEX].getceasingcumsccommtreehash(scid)['ceasingCumScTxCommTree']

        csw_proof = cswMcTest.create_test_proof("csw1",
                                                sc_csw_amount,
                                                str(scid_swapped),
                                                null,
                                                csw_mc_address,
                                                ceasingCumScTxCommTree,
                                                cert_data_hash = actCertData,
                                                constant       = constant)

        sc_csws = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid,
            "epoch": 0,
            "nullifier": null,
            "activeCertData": actCertData,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
            "scProof": csw_proof
        }]

        out_amount = sc_csw_amount / Decimal("2.0")
        sc_csw_tx_outs = {taddr: out_amount}
        rawtx = self.nodes[DISHONEST_NODE_INDEX].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[DISHONEST_NODE_INDEX].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[DISHONEST_NODE_INDEX].signrawtransaction(funded_tx['hex'])
        try:
            csw_bad = self.nodes[DISHONEST_NODE_INDEX].sendrawtransaction(sigRawtx['hex'])
            pprint.pprint(self.nodes[DISHONEST_NODE_INDEX].getrawtransaction(tx, 1))
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(f"Send csw failed with reason {errorString}", self.nodes, DEBUG_MODE)

        mark_logs(f"Check the CSW tx {csw_bad} is in mempool...", self.nodes, DEBUG_MODE)
        assert_true(csw_bad in self.nodes[DISHONEST_NODE_INDEX].getrawmempool())

        #============================================================================================
        mark_logs("\nJoining network", self.nodes, DEBUG_MODE)
        self.join_network()
        sync_blocks(self.nodes)
        mark_logs("Network joined", self.nodes, DEBUG_MODE)

        mark_logs("\nCheck that the sidechain is ceased", self.nodes, DEBUG_MODE)
        ret = self.nodes[DISHONEST_NODE_INDEX].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['state'], "ALIVE")

        mark_logs(f"Check CSW tx {csw_bad} is no more in mempool, since we crossed the epoch safeguard", self.nodes, DEBUG_MODE)
        assert_false(csw_bad in self.nodes[DISHONEST_NODE_INDEX].getrawmempool()) 

        mark_logs("And that no info are available too...", self.nodes, DEBUG_MODE)
        try:
            self.nodes[DISHONEST_NODE_INDEX].getrawtransaction(csw_bad, 1)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(f"===> {errorString}", self.nodes, DEBUG_MODE)
            assert_true("No information" in errorString)

        for i in range(NUMB_OF_NODES):
            pprint.pprint(self.nodes[i].getrawmempool())

        # if any_error this should fail
        self.nodes[MAIN_NODE].generate(1)
        self.sync_all()


if __name__ == '__main__':
    CertMempoolCleanupSplit().main()
