#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import initialize_chain_clean, assert_equal, assert_true, assert_false, \
    start_nodes, stop_nodes, get_epoch_data, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, mark_logs, \
    get_total_amount_from_listaddressgroupings, swap_bytes
from test_framework.test_framework import MINIMAL_SC_HEIGHT, MINER_REWARD_POST_H200
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal
import time
import pprint

DEBUG_MODE = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH = 5
CERT_FEE = Decimal("0.000123")
SC_FEE = Decimal("0.000345")
TX_FEE = Decimal("0.000567")
SC_COINS_MAT = 2


class sc_bwt_request(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args= [['-blockprioritysize=0',
            '-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib',
            '-scproofqueuesize=0', '-logtimemicros=1', '-sccoinsmaturity=%d' % SC_COINS_MAT]] * NUMB_OF_NODES )

        for idx, _ in enumerate(self.nodes):
            if idx < (NUMB_OF_NODES-1):
                connect_nodes_bi(self.nodes, idx, idx+1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        Node1 creates a sc1 and, after a few negative tests, some bwt requests are sent for that sc1.
        All the relevant txes (creation included) are accepted in the mempool and then mined in a block,
        with the exception of the txes with zero fee, since we started the node with the -blockprioritysize=0 option.
        Such zero fee mbtr txes are removed from the mempool whenever the safeguard of the epoch is crossed.
        A second sc is created and bwt request are sent for a zero balance sc (accepted) and a ceased sc (rejected)
        Both high level and raw versions of the command are tested.
        Finally, block disconnection is tested checking that a btr is removed from mempool if safeguard is passed
        back, as well as a cert referring to an epoch that gets unfinished by a removal of a block
        '''

        # cross-chain transfer amount
        creation_amount1 = Decimal("1.0")
        creation_amount2 = Decimal("2.0")

        ftScFee = 0
        mbtrScFee = 0
        mbtrDataLength = 1

        bwt_amount = Decimal("0.5")

        blocks = [self.nodes[0].getblockhash(0)]

        mark_logs("Node 1 generates 1 block to prepare coins to spend", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(MINIMAL_SC_HEIGHT), self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(MINIMAL_SC_HEIGHT))
        self.sync_all()

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk1  = mcTest.generate_params("sc1")
        c1 = generate_random_field_element_hex()

        vk2 = mcTest.generate_params("sc2")
        c2 = generate_random_field_element_hex()

        vk3 = mcTest.generate_params("sc3")
        c3 = generate_random_field_element_hex()

        bal_before_sc_creation = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance before SC creation: {}".format(bal_before_sc_creation), self.nodes, DEBUG_MODE)

        fee_cr1 = Decimal("0.0002")
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength":EPOCH_LENGTH,
            "toaddress":"dada",
            "amount":creation_amount1,
            "fee":fee_cr1,
            "wCertVk":vk1,
            "constant":c1,
            "mainchainBackwardTransferRequestDataLength":1
        }

        print("Node0 Chain h = ", self.nodes[0].getblockcount())

        try:
            ret = self.nodes[1].sc_create(cmdInput)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False);

        n1_net_bal = bal_before_sc_creation - creation_amount1 - fee_cr1

        cr_tx1 = ret['txid']
        scid1  = ret['scid']
        mark_logs("Node1 created the SC {} spending {} coins via tx {}.".format(scid1, creation_amount1, cr_tx1), self.nodes, DEBUG_MODE)
        self.sync_all()

        prev_epoch_block_hash = blocks[-1]

        fe1 = [generate_random_field_element_hex()]
        fe2 = [generate_random_field_element_hex()]
        fe3 = [generate_random_field_element_hex()]
        fe4 = [generate_random_field_element_hex()]

        mc_dest_addr1 = self.nodes[0].getnewaddress()
        mc_dest_addr2 = self.nodes[0].getnewaddress()
        mc_dest_addr3 = self.nodes[0].getnewaddress()
        mc_dest_addr4 = self.nodes[0].getnewaddress()

        #--- negative tests for bwt transfer request ----------------------------------------
        mark_logs("...performing some negative test...", self.nodes, DEBUG_MODE)

        # 1.  wrong scid
        outputs = [{'vScRequestData': fe1, 'scFee': SC_FEE, 'scid': "abcd", 'mcDestinationAddress': mc_dest_addr1}]
        try:
            self.nodes[1].sc_request_transfer(outputs, {})
            assert_true(False)
        except JSONRPCException as e:
            mark_logs(e.error['message'], self.nodes, DEBUG_MODE)

        # 2.  wrong mcDestinationAddress
        outputs = [{'vScRequestData': fe1, 'scFee': SC_FEE, 'scid': scid1, 'mcDestinationAddress': scid1}]
        try:
            self.nodes[1].sc_request_transfer(outputs, {})
            assert_true(False)
        except JSONRPCException as e:
            mark_logs(e.error['message'], self.nodes, DEBUG_MODE)

        # 3.  negative scfee
        outputs = [{'vScRequestData': fe1, 'scFee': Decimal("-0.2"), 'scid': scid1, 'mcDestinationAddress': mc_dest_addr1}]
        try:
            self.nodes[1].sc_request_transfer(outputs, {})
            assert_true(False)
        except JSONRPCException as e:
            mark_logs(e.error['message'], self.nodes, DEBUG_MODE)

        # 4. not including one of the mandatory param
        outputs = [{'scFee': SC_FEE, 'scid': scid1, 'mcDestinationAddress': mc_dest_addr1}]
        try:
            self.nodes[1].sc_request_transfer(outputs, {})
            assert_true(False)
        except JSONRPCException as e:
            mark_logs(e.error['message'], self.nodes, DEBUG_MODE)

        # 5.  wrong field element
        outputs = [{'vScRequestData': "abcd", 'scFee': SC_FEE, 'scid': scid1, 'mcDestinationAddress': mc_dest_addr1}]
        try:
            self.nodes[1].sc_request_transfer(outputs, {})
            assert_true(False)
        except JSONRPCException as e:
            mark_logs(e.error['message'], self.nodes, DEBUG_MODE)

        #--- end of negative tests --------------------------------------------------

        mark_logs("Node1 creates a tx with a single bwt request for sc", self.nodes, DEBUG_MODE)
        totScFee = Decimal("0.0")

        TX_FEE = Decimal("0.000123")
        outputs = [{'vScRequestData': fe1, 'scFee': SC_FEE, 'scid': scid1, 'mcDestinationAddress': mc_dest_addr1}]
        cmdParms = { "minconf":0, "fee":TX_FEE}

        try:
            bwt1 = self.nodes[1].sc_request_transfer(outputs, cmdParms)
            mark_logs("  --> bwt_tx_1 = {}.".format(bwt1), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert_true(False)

        totScFee = totScFee + SC_FEE

        n1_net_bal = n1_net_bal - SC_FEE - TX_FEE 

        self.sync_all()
        decoded_tx = self.nodes[1].getrawtransaction(bwt1, 1)

        assert_equal(len(decoded_tx['vmbtr_out']), 1)
        assert_equal(decoded_tx['vmbtr_out'][0]['mcDestinationAddress'], mc_dest_addr1)
        assert_equal(decoded_tx['vmbtr_out'][0]['scFee'], SC_FEE)
        assert_equal(decoded_tx['vmbtr_out'][0]['vScRequestData'], fe1)
        assert_equal(decoded_tx['vmbtr_out'][0]['scid'], scid1)

        mark_logs("Node1 creates a tx with the same single bwt request for sc", self.nodes, DEBUG_MODE)
        try:
            bwt2 = self.nodes[1].sc_request_transfer(outputs, cmdParms)
            mark_logs("  --> bwt_tx_2 = {}.".format(bwt2), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert_true(False)

        self.sync_all()

        totScFee = totScFee + SC_FEE

        n1_net_bal = n1_net_bal - SC_FEE - TX_FEE 
     
        mark_logs("Node0 creates a tx with a single bwt request for sc with mc_fee=0 and sc_fee=0", self.nodes, DEBUG_MODE)

        outputs = [{'vScRequestData': fe1, 'scFee': Decimal("0.0"), 'scid': scid1, 'mcDestinationAddress': mc_dest_addr1}]
        cmdParms = {"fee":0.0}
        try:
            bwt3 = self.nodes[0].sc_request_transfer(outputs, cmdParms)
            mark_logs("  --> bwt_tx_3 = {}.".format(bwt3), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()

        # verify we have in=out since no fees and no cc outputs
        decoded_tx = self.nodes[0].getrawtransaction(bwt3, 1)
        input_tx   = decoded_tx['vin'][0]['txid']
        input_tx_n = decoded_tx['vin'][0]['vout']
        decoded_input_tx = self.nodes[0].getrawtransaction(input_tx, 1)
        in_amount  = decoded_input_tx['vout'][input_tx_n]['value']
        out_amount = decoded_tx['vout'][0]['value']
        assert_equal(in_amount, out_amount)

        mark_logs("Node0 creates a tx with many bwt request for sc, one of them is repeated", self.nodes, DEBUG_MODE)
        fer = [generate_random_field_element_hex()]
        outputs = [
            {'vScRequestData': fe1, 'scFee': SC_FEE, 'scid': scid1, 'mcDestinationAddress': mc_dest_addr1},
            {'vScRequestData': fer, 'scFee': SC_FEE, 'scid': scid1, 'mcDestinationAddress': mc_dest_addr1},
            {'vScRequestData': fer, 'scFee': SC_FEE, 'scid': scid1, 'mcDestinationAddress': mc_dest_addr1}
        ]
        cmdParms = {"minconf":0, "fee":TX_FEE}
        try:
            bwt4 = self.nodes[1].sc_request_transfer(outputs, cmdParms)
            mark_logs("  --> bwt_tx_4 = {}.".format(bwt4), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()
        decoded_tx = self.nodes[0].getrawtransaction(bwt4, 1)
        assert_equal(len(decoded_tx['vmbtr_out']), 3)

        totScFee = totScFee + 3*SC_FEE
        n1_net_bal = n1_net_bal - 3*SC_FEE -TX_FEE

        # verify all bwts are in mempool together with sc creation
        assert_true(cr_tx1 in self.nodes[0].getrawmempool())
        assert_true(bwt1 in self.nodes[0].getrawmempool())
        assert_true(bwt2 in self.nodes[0].getrawmempool())
        assert_true(bwt3 in self.nodes[1].getrawmempool())
        assert_true(bwt4 in self.nodes[1].getrawmempool())

        mark_logs("Node0 confirms sc creation and bwt requests generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        curh = self.nodes[0].getblockcount()

        vtx = self.nodes[1].getblock(blocks[-1], True)['tx']
        assert_true(bwt1, bwt2 in vtx)
        assert_true(bwt2 in vtx)
        assert_true(bwt4 in vtx)

        # zero fee txes are not included in the block since we started nodes with -blockprioritysize=0
        assert_false(bwt3 in vtx)
        assert_true(bwt3 in self.nodes[0].getrawmempool())

        sc_info = self.nodes[0].getscinfo(scid1)['items'][0]

        mark_logs("Check creation and bwt requests contributed to immatureAmount of SC", self.nodes, DEBUG_MODE)
        # check immatureAmounts, both cr and btrs 
        ima = Decimal("0.0")
        for x in sc_info['immatureAmounts']:
            ima = ima + x['amount']
            assert_equal(x['maturityHeight'], curh + SC_COINS_MAT) 
        assert_equal(ima, totScFee + creation_amount1)

        #  create one more sc
        prev_epoch_hash_2 = self.nodes[0].getbestblockhash()
        epoch_len_2 = 10
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength":epoch_len_2,
            "toaddress":"dada",
            "amount":creation_amount2,
            "wCertVk":vk2,
            "constant":c2,
            'customData': "bb" * 1024,
            'forwardTransferScFee': ftScFee,
            'mainchainBackwardTransferScFee': mbtrScFee,
            'mainchainBackwardTransferRequestDataLength': mbtrDataLength
        }

        ret = self.nodes[0].sc_create(cmdInput)
        scid2  = ret['scid']
        cr_tx2 = ret['txid']
        mark_logs("Node0 created the SC2 spending {} coins via tx {}.".format(creation_amount1, cr_tx2), self.nodes, DEBUG_MODE)
        self.sync_all()

        # create a bwt request with the raw cmd version
        mark_logs("Node0 creates a tx with a bwt request using raw version of cmd", self.nodes, DEBUG_MODE)
        sc_bwt2_1 = [{'vScRequestData': fe2, 'scFee': SC_FEE, 'scid': scid2, 'mcDestinationAddress': mc_dest_addr2}]
        try:
            raw_tx = self.nodes[0].createrawtransaction([], {}, [], [], [], sc_bwt2_1)
            funded_tx = self.nodes[0].fundrawtransaction(raw_tx)
            signed_tx = self.nodes[0].signrawtransaction(funded_tx['hex'])
            bwt5 = self.nodes[0].sendrawtransaction(signed_tx['hex'])
            mark_logs("  --> bwt_tx_5 = {}.".format(bwt5), self.nodes, DEBUG_MODE)

        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()
        decoded_tx = self.nodes[0].decoderawtransaction(signed_tx['hex'])
        assert_equal(len(decoded_tx['vmbtr_out']), 1)
        assert_equal(decoded_tx['vmbtr_out'][0]['mcDestinationAddress'], mc_dest_addr2)
        assert_equal(decoded_tx['vmbtr_out'][0]['scFee'], SC_FEE)
        assert_equal(decoded_tx['vmbtr_out'][0]['vScRequestData'], fe2)
        assert_equal(decoded_tx['vmbtr_out'][0]['scid'], scid2)

        # create a bwt request with the raw cmd version with some mixed output and cc output
        mark_logs("Node0 creates a tx with a few bwt request and mixed outputs using raw version of cmd", self.nodes, DEBUG_MODE)
        outputs = { self.nodes[0].getnewaddress() :4.998 }
        sc_cr_amount = 1.0
        sc_cr = [ {"version": 0, "epoch_length":10, "amount":sc_cr_amount, "address":"effe", "wCertVk":vk3, "constant":c3} ]
        ft_amount_1 = 1.0
        ft_amount_2 = 2.0
        mc_return_address = self.nodes[0].getnewaddress()
        sc_ft = [
            {"address": "abc", "amount": ft_amount_1, "scid": scid2, "mcReturnAddress": mc_return_address},
            {"address": "cde", "amount": ft_amount_2, "scid": scid2, "mcReturnAddress": mc_return_address}
        ]
        bt_sc_fee_1 = 0.13
        bt_sc_fee_2 = 0.23
        bt_sc_fee_3 = 0.12
        sc_bwt3 = [
            {'vScRequestData': fe2, 'scFee': bt_sc_fee_1, 'scid': scid1, 'mcDestinationAddress': mc_dest_addr2},
            {'vScRequestData': fe3, 'scFee': bt_sc_fee_2, 'scid': scid2, 'mcDestinationAddress': mc_dest_addr3},
            {'vScRequestData': fe4, 'scFee': bt_sc_fee_3, 'scid': scid2, 'mcDestinationAddress': mc_dest_addr4}
        ]
        try:
            raw_tx = self.nodes[0].createrawtransaction([], outputs, [], sc_cr, sc_ft, sc_bwt3)
            funded_tx = self.nodes[0].fundrawtransaction(raw_tx)
            signed_tx = self.nodes[0].signrawtransaction(funded_tx['hex'])
            bwt6 = self.nodes[0].sendrawtransaction(signed_tx['hex'])
            mark_logs("  --> bwt_tx_6 = {}.".format(bwt6), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)


        totScFee = totScFee + Decimal("0.13")

        self.sync_all()
        decoded_tx = self.nodes[0].decoderawtransaction(signed_tx['hex'])
        assert_equal(len(decoded_tx['vsc_ccout']), 1)
        assert_equal(len(decoded_tx['vft_ccout']), 2)
        assert_equal(len(decoded_tx['vmbtr_out']), 3)
        assert_equal(decoded_tx['vmbtr_out'][0]['vScRequestData'], fe2)
        assert_equal(decoded_tx['vmbtr_out'][1]['vScRequestData'], fe3)
        assert_equal(decoded_tx['vmbtr_out'][2]['vScRequestData'], fe4)

        # Check transaction details
        tx_details = self.nodes[0].gettransaction(bwt6)["details"]
        assert_equal(3, len(tx_details))
        assert_equal(tx_details[1]["category"], "send")
        assert_equal(tx_details[1]["account"], "")
        assert_equal(float(tx_details[1]["amount"]), -(sc_cr_amount + ft_amount_1 + ft_amount_2 + bt_sc_fee_1 + bt_sc_fee_2 + bt_sc_fee_3))

        # verify all bwts are in mempool
        assert_true(bwt3 in self.nodes[0].getrawmempool())
        assert_true(bwt5 in self.nodes[0].getrawmempool())
        assert_true(bwt6 in self.nodes[0].getrawmempool())

        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        vtx = self.nodes[1].getblock(blocks[-1], True)['tx']
        assert_true(bwt5 in vtx)
        assert_true(bwt6 in vtx)

        mark_logs("Node0 generates {} blocks maturing SC2 creation amount".format(SC_COINS_MAT), self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(SC_COINS_MAT))
        self.sync_all()

        cur_h = self.nodes[0].getblockcount()

        ret_sc1 = self.nodes[0].getscinfo(scid1, False, False)['items'][0]
        ret_sc2 = self.nodes[0].getscinfo(scid2, False, False)['items'][0]

        # get the submission window limit for SC1
        sc1_cr_height=ret_sc1['createdAtBlockHeight']
        sc1_ceas_h = ret_sc1['ceasingHeight']
        sc1_ceas_limit_delta = sc1_ceas_h - cur_h - 1
        mark_logs("current height={}, creation height={}, ceasingHeight={}, ceas_limit_delta={}, epoch_len={}"
            .format(cur_h, sc1_cr_height, sc1_ceas_h, sc1_ceas_limit_delta, EPOCH_LENGTH), self.nodes, DEBUG_MODE)

        mark_logs("Node0 generates {} blocks reaching the submission window height for SC1".format(sc1_ceas_limit_delta), self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(sc1_ceas_limit_delta))
        self.sync_all()

        print("Node0 Chain h = ", self.nodes[0].getblockcount())

        # the zero fee mbtr tx has not yet been removed from mempool since we reached epoch safe guard but
        # we are still in the epoch 0
        mark_logs("Check btr is still in mempool", self.nodes, DEBUG_MODE)
        assert_true(bwt3 in self.nodes[0].getrawmempool())

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid1, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        #empty sc1 balance
        bwt_amount = creation_amount1
        amounts = [{"address": mc_dest_addr2, "amount": bwt_amount}]
        scid1_swapped = str(swap_bytes(scid1))

        proof = mcTest.create_test_proof(
            "sc1", scid1_swapped, epoch_number, 0, mbtrScFee, ftScFee, epoch_cum_tree_hash, c1, [mc_dest_addr2], [bwt_amount])

        mark_logs("Node1 sends a cert withdrawing the contribution of the creation amount to the sc balance", self.nodes, DEBUG_MODE)
        try:
            cert_epoch_0 = self.nodes[1].sc_send_certificate(scid1, epoch_number, 0,
                epoch_cum_tree_hash, proof, amounts, ftScFee, mbtrScFee, CERT_FEE)
            mark_logs("Node 1 sent a cert with bwd transfer of {} coins to Node1 address via cert {}.".format(bwt_amount, cert_epoch_0), self.nodes, DEBUG_MODE)
            assert(len(cert_epoch_0) > 0)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()
        n1_net_bal = n1_net_bal - CERT_FEE

        mark_logs("Node0 confirms cert generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        #
        # Check removed because MBTR does not depend on cert data hash anymore
        #
        # print("Node0 Chain h = ", self.nodes[0].getblockcount())
        # mark_logs("Check btr {} is removed from mempool".format(bwt3), self.nodes, DEBUG_MODE)
        # assert_false(bwt3 in self.nodes[0].getrawmempool())

        mark_logs("Checking SC1 balance is nothing but sc fees of the various mbtr", self.nodes, DEBUG_MODE)
        sc_post_bwd = self.nodes[0].getscinfo(scid1, False, False)['items'][0]
        assert_equal(sc_post_bwd["balance"], totScFee)

        ceasing_h = int(sc_post_bwd['ceasingHeight'])
        current_h = self.nodes[0].getblockcount()

        mark_logs("Node0 generates {} blocks moving on the ceasing limit of SC1".format(ceasing_h - current_h - 1), self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(ceasing_h - current_h - 1))
        self.sync_all()

        outputs = [{'vScRequestData': fe1, 'scFee': SC_FEE, 'scid': scid1, 'mcDestinationAddress': mc_dest_addr3}]
        cmdParms = { "minconf":0, "fee":0.0}
        mark_logs("Node0 creates a tx with a bwt request for a sc with null balance", self.nodes, DEBUG_MODE)
        try:
            bwt7 = self.nodes[1].sc_request_transfer(outputs, cmdParms)
            mark_logs("  --> bwt_tx_7 = {}.".format(bwt7), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()

        assert_true(bwt7 in self.nodes[0].getrawmempool())

        # zero fee txes are not included in the block since we started nodes with -blockprioritysize=0
        #totScFee = totScFee + SC_FEE
        n1_net_bal = n1_net_bal - SC_FEE 
        assert_equal(n1_net_bal, Decimal(self.nodes[1].z_gettotalbalance(0)['total']))
        # check also other commands which are handling balance, just for non-regressions
        assert_equal(n1_net_bal, Decimal(self.nodes[1].listaccounts()[""]))
        lag_list = self.nodes[1].listaddressgroupings()
        assert_equal(n1_net_bal, get_total_amount_from_listaddressgroupings(lag_list))

        mark_logs("Node0 generates 1 block, thus ceasing SC 1", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        assert_equal(self.nodes[0].getscinfo(scid1)['items'][0]["state"], "CEASED")
        assert_equal(self.nodes[0].getscinfo(scid2)['items'][0]["state"], "ALIVE")

        # check mbtr is not in the block just mined
        vtx = self.nodes[0].getblock(blocks[-1], True)['tx']
        assert_false(bwt7 in vtx)

        # and not in the mempool either
        assert_false(bwt7 in self.nodes[0].getrawmempool())

        # the scFee contribution of the removed tx has been restored to the Node balance
        n1_net_bal = n1_net_bal + SC_FEE 

        outputs = [{'vScRequestData': fe1, 'scFee': SC_FEE, 'scid': scid1, 'mcDestinationAddress': mc_dest_addr1}]
        cmdParms = {'minconf':0, 'fee':TX_FEE}
        mark_logs("Node1 creates a tx with a bwt request for a ceased sc (should fail)", self.nodes, DEBUG_MODE)
        try:
            self.nodes[1].sc_request_transfer(outputs, cmdParms)
            assert_true(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        sc_pre_restart = self.nodes[0].getscinfo(scid1)['items'][0]
        assert_equal(sc_pre_restart["balance"], totScFee)

        mark_logs("Checking data persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        assert_equal(n1_net_bal, Decimal(self.nodes[1].z_gettotalbalance(0)['total']))
        # check also other commands handling balance, just for non-regressions
        assert_equal(n1_net_bal, Decimal(self.nodes[1].listaccounts()[""]))
        lag_list = self.nodes[1].listaddressgroupings()
        assert_equal(n1_net_bal, get_total_amount_from_listaddressgroupings(lag_list))
        
        sc_post_restart = self.nodes[0].getscinfo(scid1)['items'][0]
        assert_equal(sc_pre_restart, sc_post_restart)

        assert_equal(self.nodes[0].getscinfo(scid1)['items'][0]["state"], "CEASED")
        assert_equal(self.nodes[0].getscinfo(scid2)['items'][0]["state"], "ALIVE")

        ret = self.nodes[0].getscinfo(scid2, False, False)['items'][0]

        ceasing_h = int(ret['ceasingHeight'])
        current_h = self.nodes[0].getblockcount()

        mark_logs("Node0 generates {} blocks moving on the ceasing limit of SC2".format(ceasing_h - current_h - 1), self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(ceasing_h - current_h - 1))
        self.sync_all()
        print("Current height = ",  self.nodes[0].getblockcount())

        any_error = False

        # 1) send a cert
        mark_logs("\nNode0 sends a certificate to SC2", self.nodes, DEBUG_MODE)
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid2, self.nodes[0], epoch_len_2)
        sc_creating_height = self.nodes[0].getscinfo(scid2)['items'][0]['createdAtBlockHeight']
        epoch_block_hash = self.nodes[0].getblockhash(sc_creating_height - 1 + ((epoch_number + 1) * epoch_len_2))

        bt_amount = Decimal("1.0")
        addr_node1 = self.nodes[1].getnewaddress()
        quality = 10
        scid2_swapped = str(swap_bytes(scid2))

        proof = mcTest.create_test_proof(
            "sc2", scid2_swapped, epoch_number, quality, mbtrScFee, ftScFee, epoch_cum_tree_hash, c2, [addr_node1], [bt_amount])
 
        amount_cert = [{"address": addr_node1, "amount": bt_amount}]
        try:
            cert_bad = self.nodes[0].sc_send_certificate(scid2, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert, ftScFee, mbtrScFee, 0.01)
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Send certificate failed with reason {}".format(errorString))
            assert(False)
        self.sync_all()

        mark_logs("Check cert {} is in mempool".format(cert_bad), self.nodes, DEBUG_MODE)
        assert_true(cert_bad in self.nodes[0].getrawmempool()) 

        # 2) generate a block crossing the submission windows limit
        mark_logs("Node0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        #
        # Check removed because MBTR does not depend on cert data hash anymore
        #
        # 3) add a mbwt. We use another node for avoiding dependancies between each other's txes and cert
        # mark_logs("Node1 creates a tx with a bwt request", self.nodes, DEBUG_MODE)
        # outputs = [{'vScRequestData':fe1, 'scFee':Decimal("0.001"), 'scid':scid2, 'pubkeyhash':pkh1 }]
        # cmdParms = { "minconf":0, "fee":0.0}
        # try:
        #     tx_bwt = self.nodes[1].sc_request_transfer(outputs, cmdParms);
        # except JSONRPCException as e:
        #     errorString = e.error['message']
        #     mark_logs(errorString,self.nodes,DEBUG_MODE)
        #     assert_true(False)
        # self.sync_all()

        # mark_logs("Check bwd tx {} is in mempool".format(tx_bwt), self.nodes, DEBUG_MODE)
        # assert_true(tx_bwt in self.nodes[0].getrawmempool()) 

        # 4) invalidate
        mark_logs("Node1 invalidates one block", self.nodes, DEBUG_MODE)
        self.nodes[1].invalidateblock(self.nodes[1].getbestblockhash())

        #
        # Check removed because MBTR does not depend on cert data hash anymore
        #
        # check mbtr is not in the mempool because it refers to an invalid cert data hash
        # mark_logs("Check bwd tx {} is not in mempool anymore".format(tx_bwt), self.nodes, DEBUG_MODE)
        # if tx_bwt in self.nodes[1].getrawmempool():
        #     print "FIX FIX FIX!!! bwt is still in mempool" 
        #     any_error = True

        mark_logs("Node1 invalidates till to the previous epoch", self.nodes, DEBUG_MODE)
        self.nodes[1].invalidateblock(epoch_block_hash)

        mark_logs("Check cert {} is not in mempool anymore".format(cert_bad), self.nodes, DEBUG_MODE)
        assert_false(cert_bad in self.nodes[1].getrawmempool()) 

        ret = self.nodes[1].getscinfo(scid2, False, False)['items'][0]
        assert_equal(ret, ret_sc2)

        if any_error:
            print(" =========================> Test failed!!!")
            assert(False)





if __name__ == '__main__':
    sc_bwt_request().main()
