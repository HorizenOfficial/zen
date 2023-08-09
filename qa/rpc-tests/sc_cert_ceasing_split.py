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
    wait_bitcoinds, stop_nodes, get_epoch_data, sync_mempools, sync_blocks, \
    advance_epoch, swap_bytes

from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.mc_test.mc_test import *

from decimal import Decimal
import pprint
import time

NUMB_OF_NODES = 4
DEBUG_MODE = 1
EPOCH_LENGTH = 10
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')

# Create one-input, one-output, no-fee transaction:
class CeasingSplitTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_nodes(self):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[['-scproofqueuesize=0', '-logtimemicros=1', '-debug=sc', '-debug=py',
                                              '-debug=mempool', '-debug=net', '-debug=bench']] * NUMB_OF_NODES)

    def run_test(self):
        '''
        Create a SC, advance two epochs, move to the limit of the safe guard and then split the network. 
        One network part sends fwt, mbtr and a certificate
        The other network part mines one block making the SC cease.
        When the network is joined, verify that the SC is ceased and all the txes/cet sent by the loosing first network part
        have been removed from the mempool.
        '''

        # prepare some coins 
        self.nodes[2].generate(1)
        self.sync_all()
        self.nodes[1].generate(1)
        self.sync_all()
        self.nodes[0].generate(ForkHeights['MINIMAL_SC']-2)
        self.sync_all()
        block_2 = self.nodes[0].getbestblockhash()
        self.nodes[0].generate(1)
        self.sync_all()
        prev_epoch_hash = self.nodes[0].getbestblockhash()
        block_1 = prev_epoch_hash

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
            'constant': constant,
            'mainchainBackwardTransferRequestDataLength': 1
        }

        res = self.nodes[0].sc_create(cmdInput)
        tx =   res['txid']
        scid = res['scid']
        self.sync_all()
        mark_logs("tx {} created SC {}".format(tx, scid), self.nodes, DEBUG_MODE)

        mc_dest_addr = self.nodes[1].getnewaddress()
        fe1 = [generate_random_field_element_hex()]

        # advance two epochs
        mark_logs("\nLet 2 epochs pass by...".  format(sc_epoch_len), self.nodes, DEBUG_MODE)

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

        mark_logs("\nNode0 generates {} block reaching the sg for the next epoch".format(numbBlocks), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(numbBlocks)
        self.sync_all()

        cur_h = self.nodes[0].getblockcount()
        ret=self.nodes[0].getscinfo(scid, True, False)['items'][0]
        cr_height=ret['createdAtBlockHeight']
        ceas_h = ret['ceasingHeight']
        mark_logs("epoch number={}, current height={}, creation height={}, ceasingHeight={}, epoch_len={}"
            .format(epoch_number, cur_h, cr_height, ceas_h, sc_epoch_len), self.nodes, DEBUG_MODE)
        print

        bal_initial = self.nodes[0].getscinfo(scid, False, False)['items'][0]['balance']

        #============================================================================================
        mark_logs("\nSplit network", self.nodes, DEBUG_MODE)
        self.split_network(2)
        mark_logs("The network is split: 0-1-2 .. 3", self.nodes, DEBUG_MODE)

        # Network part 0-1-2
        print("------------------")
        # use different nodes for sending txes and cert in order to be sure there are no dependancies from each other
        fwt_amount = Decimal("2.0")
        mc_return_address = self.nodes[0].getnewaddress()
        mark_logs("\nNTW part 1) Node0 sends {} coins to SC".format(fwt_amount), self.nodes, DEBUG_MODE)
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount, "scid": scid, 'mcReturnAddress': mc_return_address}]
        tx_fwd = self.nodes[0].sc_send(cmdInput)
        sync_mempools(self.nodes[0:3])

        mark_logs("              Check fwd tx {} is in mempool".format(tx_fwd), self.nodes, DEBUG_MODE)
        assert_true(tx_fwd in self.nodes[0].getrawmempool()) 

        outputs = [{'vScRequestData':fe1, 'scFee':Decimal("0.001"), 'scid':scid, 'mcDestinationAddress': mc_dest_addr}]
        cmdParms = { "minconf":0, "fee":0.0}
        mark_logs("\nNTW part 1) Node1 creates a tx with a bwt request", self.nodes, DEBUG_MODE)
        try:
            tx_bwt = self.nodes[1].sc_request_transfer(outputs, cmdParms)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        sync_mempools(self.nodes[0:3])

        mark_logs("              Check bwd tx {} is in mempool".format(tx_bwt), self.nodes, DEBUG_MODE)
        assert_true(tx_bwt in self.nodes[0].getrawmempool()) 

        mark_logs("\nNTW part 1) Node2 sends a certificate for keeping the SC alive", self.nodes, DEBUG_MODE)
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
                                             constant = constant,
                                             pks      = [addr_node1],
                                             amounts  = [bt_amount])
 
        amount_cert = [{"address": addr_node1, "amount": bt_amount}]
        try:
            cert_bad = self.nodes[2].sc_send_certificate(scid, epoch_number, 10,
                epoch_cum_tree_hash, proof, amount_cert, 0, 0, 0.01)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Send certificate failed with reason {}".format(errorString))
            assert(False)

        sync_mempools(self.nodes[0:3])

        mark_logs("              Check cert {} is in mempool".format(cert_bad), self.nodes, DEBUG_MODE)
        assert_true(cert_bad in self.nodes[0].getrawmempool()) 

        # Network part 2
        print("------------------")

        mark_logs("\nNTW part 2) Let SC cease... ".format(scid), self.nodes, DEBUG_MODE)

        mark_logs("NTW part 2) Node3 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[3].generate(1)
        sync_mempools(self.nodes[3:4])

        # check it is really ceased
        ret = self.nodes[3].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")

        #============================================================================================
        mark_logs("\nJoining network", self.nodes, DEBUG_MODE)
        self.join_network(2)
        time.sleep(2)
        mark_logs("Network joined", self.nodes, DEBUG_MODE)

        any_error = False

        # check SC is really ceased
        mark_logs("Check that Node3 has prevailed and SC is really ceased...".format(tx_fwd), self.nodes, DEBUG_MODE)
        ret = self.nodes[0].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")

        mark_logs("Check fwd tx {} is no more in mempool...".format(tx_fwd), self.nodes, DEBUG_MODE)

        #assert_false(tx_fwd in self.nodes[0].getrawmempool()) 
        if tx_fwd in self.nodes[0].getrawmempool():
            print("FIX FIX FIX!!! fwt is still in mempool" )
            any_error = True


        mark_logs("And that no info are available too...".format(tx_fwd), self.nodes, DEBUG_MODE)
        try:
            dec = self.nodes[0].getrawtransaction(tx_fwd, 1)
            print("FIX FIX FIX!!! tx_fwd has info in Node0" )
            any_error = True
            #assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("===> {}".format(errorString), self.nodes, DEBUG_MODE)
            assert_true("No information" in errorString)

        mark_logs("Check bwd tx {} is no more in mempool".format(tx_bwt), self.nodes, DEBUG_MODE)
        #assert_false(tx_bwt in self.nodes[0].getrawmempool()) 
        if tx_bwt in self.nodes[0].getrawmempool():
            print("FIX FIX FIX!!! bwt is still in mempool" )
            any_error = True

        mark_logs("And that no info are available too...".format(tx_bwt), self.nodes, DEBUG_MODE)
        try:
            dec = self.nodes[0].getrawtransaction(tx_bwt, 1)
            print("FIX FIX FIX!!! tx_bwt has info in Node0" )
            any_error = True
            #assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("===> {}".format(errorString), self.nodes, DEBUG_MODE)
            assert_true("No information" in errorString)

        mark_logs("Check cert {} is no more in mempool".format(cert_bad), self.nodes, DEBUG_MODE)
        #assert_false(cert in self.nodes[0].getrawmempool()) 
        if cert_bad in self.nodes[0].getrawmempool():
            print("FIX FIX FIX!!! cert is still in mempool" )

        mark_logs("And that no info are available too...", self.nodes, DEBUG_MODE)
        try:
            dec = self.nodes[0].getrawtransaction(cert_bad, 1)
            print("FIX FIX FIX!!! cert has info in Node0" )
            any_error = True
            #assert (False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("===> {}".format(errorString), self.nodes, DEBUG_MODE)

        mark_logs("Check SC balance has not changed", self.nodes, DEBUG_MODE)
        try:
            bal_final = self.nodes[0].getscinfo(scid)['items'][0]['balance']
            assert_equal(bal_initial, bal_final)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("===> {}".format(errorString), self.nodes, DEBUG_MODE)

        pprint.pprint(self.nodes[0].getrawmempool())

        if any_error:
            print(" =========================> Test failed!!!")
            assert(False)

        # if any_error this should fail
        #self.nodes[0].generate(1)


if __name__ == '__main__':
    CeasingSplitTest().main()
