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
    wait_bitcoinds, stop_nodes, get_epoch_data, sync_mempools, sync_blocks, \
    disconnect_nodes, advance_epoch, swap_bytes

from test_framework.test_framework import ForkHeights
from test_framework.mc_test.mc_test import CertTestUtils, CSWTestUtils, generate_random_field_element_hex

from decimal import Decimal
import pprint
import time

NUMB_OF_NODES = 4
DEBUG_MODE = 1
EPOCH_LENGTH = 10
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')

# Create one-input, one-output, no-fee transaction:
class CertMempoolCleanupSplit(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[['-logtimemicros=1', '-scproofqueuesize=0', '-debug=sc', '-debug=py',
                                              '-debug=mempool', '-debug=net', '-debug=bench']] * NUMB_OF_NODES)

        if not split:
            # 2 and 3 are joint only if split==false
            connect_nodes_bi(self.nodes, 2, 3)
            sync_blocks(self.nodes[2:4])
            sync_mempools(self.nodes[2:4])

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        self.is_network_split = split
        self.sync_all()

    def split_network(self):
        # Split the network of three nodes into nodes 0-1-2 and 3.
        assert not self.is_network_split
        disconnect_nodes(self.nodes[2], 3)
        disconnect_nodes(self.nodes[3], 2)
        self.is_network_split = True

    def join_network(self):
        # Join the (previously split) network pieces together: 0-1-2-3
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 2, 3)
        connect_nodes_bi(self.nodes, 3, 2)
        time.sleep(2)
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
        self.nodes[3].generate(1)
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()
        self.nodes[0].generate(ForkHeights['MINIMAL_SC']-3)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        print("Node0 Chain h = ", self.nodes[0].getblockcount())

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

        res = self.nodes[0].sc_create(cmdInput)
        tx =   res['txid']
        scid = res['scid']
        self.sync_all()
        mark_logs("tx {} created SC {}".format(tx, scid), self.nodes, DEBUG_MODE)

        # advance two epochs
        mark_logs("\nLet 2 epochs pass by...", self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
             scid, "sc1", constant, sc_epoch_len)

        mark_logs("\n==> certificate for epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
             scid, "sc1", constant, sc_epoch_len)

        mark_logs("\n==> certificate for epoch {} {}l".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        ceas_height = self.nodes[0].getscinfo(scid, False, False)['items'][0]['ceasingHeight']
        numbBlocks = ceas_height - self.nodes[0].getblockcount() + sc_epoch_len - 1
        print("Node0 Chain h = ", self.nodes[0].getblockcount())

        mark_logs("\nNode0 generates {} block reaching the sg for the next epoch".format(numbBlocks), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(numbBlocks)
        self.sync_all()
        print("Node0 Chain h = ", self.nodes[0].getblockcount())
        
        #============================================================================================
        mark_logs("\nSplit network", self.nodes, DEBUG_MODE)
        self.split_network()
        mark_logs("The network is split: 0-1-2 .. 3", self.nodes, DEBUG_MODE)

        # Network part 0-1-2
        print("------------------")

        mark_logs("\nNTW part 1) Node2 sends a certificate", self.nodes, DEBUG_MODE)
        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[2], sc_epoch_len)

        bt_amount = Decimal("5.0")
        addr_node1 = self.nodes[1].getnewaddress()
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
            cert_bad = self.nodes[2].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert, 0, 0, 0.01)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Send certificate failed with reason {}".format(errorString))
            assert(False)
        sync_mempools(self.nodes[0:3])

        mark_logs("Check cert {} is in mempool".format(cert_bad), self.nodes, DEBUG_MODE)
        assert_true(cert_bad in self.nodes[0].getrawmempool())

        mark_logs("Generates two blocks to make the chain longer than sub-network 2", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(2)

        print("Node0 Chain h = ", self.nodes[0].getblockcount())

        # Network part 2
        #------------------

        mark_logs("\nNTW part 2) Node3 generates one block to cease the sidechain", self.nodes, DEBUG_MODE)
        self.nodes[3].generate(1)

        mark_logs("Check that the sidechain is ceased from node 3 perspective", self.nodes, DEBUG_MODE)
        ret = self.nodes[3].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")

        sc_bal = ret['balance']

        mark_logs("Node 3 creates a CSW transaction", self.nodes, DEBUG_MODE)

        csw_mc_address = self.nodes[3].getnewaddress()
        taddr = self.nodes[3].getnewaddress()
        sc_csw_amount = sc_bal
        null = generate_random_field_element_hex()
        actCertData            = self.nodes[3].getactivecertdatahash(scid)['certDataHash']
        ceasingCumScTxCommTree = self.nodes[3].getceasingcumsccommtreehash(scid)['ceasingCumScTxCommTree']

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
        rawtx = self.nodes[3].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[3].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[3].signrawtransaction(funded_tx['hex'])
        try:
            csw_bad = self.nodes[3].sendrawtransaction(sigRawtx['hex'])
            pprint.pprint(self.nodes[3].getrawtransaction(tx, 1))
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send csw failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        mark_logs("Check the CSW tx {} is in mempool...".format(csw_bad), self.nodes, DEBUG_MODE)
        assert_true(csw_bad in self.nodes[3].getrawmempool())

        #============================================================================================
        mark_logs("\nJoining network", self.nodes, DEBUG_MODE)
        self.join_network()
        mark_logs("Network joined", self.nodes, DEBUG_MODE)

        mark_logs("\nCheck that the sidechain is ceased", self.nodes, DEBUG_MODE)
        ret = self.nodes[3].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['state'], "ALIVE")

        mark_logs("Check CSW tx {} is no more in mempool, since we crossed the epoch safeguard".format(csw_bad), self.nodes, DEBUG_MODE)
        assert_false(csw_bad in self.nodes[3].getrawmempool()) 

        mark_logs("And that no info are available too...", self.nodes, DEBUG_MODE)
        try:
            self.nodes[3].getrawtransaction(csw_bad, 1)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("===> {}".format(errorString), self.nodes, DEBUG_MODE)
            assert_true("No information" in errorString)

        for i in range(NUMB_OF_NODES):
            pprint.pprint(self.nodes[i].getrawmempool())

        # if any_error this should fail
        self.nodes[0].generate(1)
        self.sync_all()


if __name__ == '__main__':
    CertMempoolCleanupSplit().main()
