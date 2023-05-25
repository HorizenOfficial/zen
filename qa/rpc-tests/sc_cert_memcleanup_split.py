#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test checkcswnullifier for presence nullifer in MC.
#

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, connect_nodes_bi, assert_true, assert_false, mark_logs, \
    get_epoch_data, sync_mempools, sync_blocks, \
    disconnect_nodes, advance_epoch, swap_bytes

from test_framework.test_framework import ForkHeights
from test_framework.mc_test.mc_test import *

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
        One network part sends fwt, mbtr and a certificate, all of them are stored in mempool.
        The other network part sends a certificate and mines one block reaching a longer chain height.
        When the network is joined, verify that the SC is alive, fwt is still in the mempool of the the
        loosing network part, but cert and mbtr have been removed from those mempool.
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

        # generate wCertVk and constant
        vk = certMcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()

        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': sc_epoch_len,
            'amount': sc_cr_amount,
            'toaddress': sc_address,
            'wCertVk': vk,
            'constant': constant,
            'mainchainBackwardTransferRequestDataLength': 1
        }

        res = self.nodes[MAIN_NODE].sc_create(cmdInput)
        tx =   res['txid']
        scid = res['scid']
        scid_swapped = str(swap_bytes(scid))
        self.sync_all()
        mark_logs(f"tx {tx} created SC {scid}", self.nodes, DEBUG_MODE)

        dest_addr = self.nodes[HONEST_NODES[1]].getnewaddress()
        fe1 = [generate_random_field_element_hex()]

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
        
        bal_initial = self.nodes[MAIN_NODE].getscinfo(scid, False, False)['items'][0]['balance']

        #============================================================================================
        mark_logs("\nSplit network", self.nodes, DEBUG_MODE)
        self.split_network()
        mark_logs(f"The network is split: {HONEST_NODES[0]}-{HONEST_NODES[1]}-{HONEST_NODES[2]} .. {DISHONEST_NODE_INDEX}", self.nodes, DEBUG_MODE)

        # Network part 0-1-2
        print("------------------")
        # use different nodes for sending txes and cert in order to be sure there are no dependancies from each other
        fwt_amount = Decimal("2.0")
        mc_return_address = self.nodes[MAIN_NODE].getnewaddress()
        mark_logs(f"\nNTW part 1) Node {MAIN_NODE} sends {fwt_amount} coins to SC", self.nodes, DEBUG_MODE)
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount, "scid": scid, 'mcReturnAddress': mc_return_address}]
        tx_fwd = self.nodes[MAIN_NODE].sc_send(cmdInput)
        sync_mempools([self.nodes[i] for i in HONEST_NODES])

        mark_logs(f"              Check fwd tx {tx_fwd} is in mempool", self.nodes, DEBUG_MODE)
        assert_true(tx_fwd in self.nodes[MAIN_NODE].getrawmempool()) 

        outputs = [{'vScRequestData': fe1, 'scFee': Decimal("0.001"), 'scid': scid, 'mcDestinationAddress': dest_addr}]
        cmdParms = { "minconf":0, "fee":0.0}
        mark_logs(f"\nNTW part 1) Node {HONEST_NODES[1]} creates a tx with a bwt request", self.nodes, DEBUG_MODE)
        try:
            tx_bwt = self.nodes[HONEST_NODES[1]].sc_request_transfer(outputs, cmdParms)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        sync_mempools([self.nodes[i] for i in HONEST_NODES])

        mark_logs(f"              Check bwd tx {tx_bwt} is in mempool", self.nodes, DEBUG_MODE)
        assert_true(tx_bwt in self.nodes[MAIN_NODE].getrawmempool()) 

        mark_logs(f"\nNTW part 1) Node {HONEST_NODES[2]} sends a certificate", self.nodes, DEBUG_MODE)
        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[HONEST_NODES[2]], sc_epoch_len)

        bt_amount = Decimal("5.0")
        addr_node1 = self.nodes[HONEST_NODES[1]].getnewaddress()
        quality = 10

        proof = certMcTest.create_test_proof("sc1",
                                             scid_swapped,
                                             epoch_number,
                                             quality,
                                             MBTR_SC_FEE,
                                             FT_SC_FEE,
                                             epoch_cum_tree_hash,
                                             constant = constant,
                                             pks      = [addr_node1],
                                             amounts  = [bt_amount])

        amount_cert = [{"address": addr_node1, "amount": bt_amount}]
        try:
            cert_bad = self.nodes[HONEST_NODES[2]].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert, 0, 0, 0.01)
        except JSONRPCException as e:
            errorString = e.error['message']
            print(f"Send certificate failed with reason {errorString}")
            assert(False)
        sync_mempools([self.nodes[i] for i in HONEST_NODES])

        mark_logs(f"              Check cert {cert_bad} is in mempool", self.nodes, DEBUG_MODE)
        assert_true(cert_bad in self.nodes[MAIN_NODE].getrawmempool()) 
        print(f"Node {MAIN_NODE} Chain h = {self.nodes[MAIN_NODE].getblockcount()}")

        # Network part 2
        #------------------
        mark_logs(f"\nNTW part 2) Dishonest node {DISHONEST_NODE_INDEX} sends a certificate", self.nodes, DEBUG_MODE)
        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[DISHONEST_NODE_INDEX], sc_epoch_len)

        bt_amount_2 = Decimal("10.0")
        addr_node1 = self.nodes[HONEST_NODES[1]].getnewaddress()
        quality = 5

        proof = certMcTest.create_test_proof("sc1",
                                             scid_swapped,
                                             epoch_number,
                                             quality,
                                             MBTR_SC_FEE,
                                             FT_SC_FEE,
                                             epoch_cum_tree_hash,
                                             constant = constant,
                                             pks      = [addr_node1],
                                             amounts  = [bt_amount_2])

        amount_cert = [{"address": addr_node1, "amount": bt_amount_2}]
        try:
            cert = self.nodes[DISHONEST_NODE_INDEX].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert, 0, 0, 0.01)
        except JSONRPCException as e:
            errorString = e.error['message']
            print(f"Send certificate failed with reason {errorString}")
            assert(False)
        sync_mempools([self.nodes[DISHONEST_NODE_INDEX]])

        mark_logs(f"              Check cert {cert} is in mempool", self.nodes, DEBUG_MODE)
        assert_true(cert in self.nodes[DISHONEST_NODE_INDEX].getrawmempool())
        print(f"Dishonest node {DISHONEST_NODE_INDEX} Chain h = {self.nodes[DISHONEST_NODE_INDEX].getblockcount()}")


        mark_logs(f"Dishonest node {DISHONEST_NODE_INDEX} generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[DISHONEST_NODE_INDEX].generate(1)
        sync_mempools([self.nodes[DISHONEST_NODE_INDEX]])
        print(f"Dishonest node {DISHONEST_NODE_INDEX} Chain h = {self.nodes[DISHONEST_NODE_INDEX].getblockcount()}")

        #============================================================================================
        mark_logs("\nJoining network", self.nodes, DEBUG_MODE)
        self.join_network()
        sync_blocks(self.nodes)
        mark_logs("Network joined", self.nodes, DEBUG_MODE)

        # check SC is alive
        ret = self.nodes[DISHONEST_NODE_INDEX].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['state'], "ALIVE")

        mark_logs(f"Check fwd tx {tx_fwd} is still in mempool...", self.nodes, DEBUG_MODE)
        assert_true(tx_fwd in self.nodes[MAIN_NODE].getrawmempool()) 

        mark_logs(f"Check bwd tx {tx_bwt} is still in mempool, even though we crossed the epoch safeguard", self.nodes, DEBUG_MODE)
        assert_true(tx_bwt in self.nodes[MAIN_NODE].getrawmempool()) 

        mark_logs(f"Check cert {cert_bad} is no more in mempool, since we crossed the epoch safeguard", self.nodes, DEBUG_MODE)
        assert_false(cert_bad in self.nodes[MAIN_NODE].getrawmempool()) 

        mark_logs("And that no info are available too...", self.nodes, DEBUG_MODE)
        try:
            self.nodes[MAIN_NODE].getrawtransaction(cert_bad, 1)
            assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(f"===> {errorString}", self.nodes, DEBUG_MODE)

        mark_logs("Check SC balance has been properly changed", self.nodes, DEBUG_MODE)
        try:
            bal_final = self.nodes[MAIN_NODE].getscinfo(scid)['items'][0]['balance']
            assert_equal(bal_initial - bt_amount_2, bal_final)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(f"===> {errorString}", self.nodes, DEBUG_MODE)

        for i in range(NUMB_OF_NODES):
            pprint.pprint(self.nodes[i].getrawmempool())

        # if any_error this should fail
        self.nodes[MAIN_NODE].generate(1)
        self.sync_all()


if __name__ == '__main__':
    CertMempoolCleanupSplit().main()
