#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_true, assert_false, initialize_chain_clean, \
    stop_nodes, wait_bitcoinds, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, disconnect_nodes, mark_logs, \
    dump_sc_info_record, get_epoch_data, swap_bytes, advance_epoch
from test_framework.mc_test.mc_test import CertTestUtils, generate_random_field_element_hex
import os
from decimal import Decimal
import pprint
import time

NUMB_OF_NODES = 4
DEBUG_MODE = 1
EPOCH_LENGTH = 10
CERT_FEE = Decimal('0.015') # high fee rate
BLK_MAX_SZ = 5000
BLK_MIN_SZ = 2000000
NUM_BLOCK_FOR_SC_FEE_CHECK = EPOCH_LENGTH+1

class SCStaleFtAndMbtrTest(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        extra_args=[
            ['-scproofqueuesize=0', '-logtimemicros=1',
             '-debug=sc', '-debug=py', '-debug=mempool', '-debug=net', '-debug=bench',
             '-blockprioritysize=100',
             '-blockmaxsize=%d'%BLK_MAX_SZ,
             '-blocksforscfeecheck=%d'%NUM_BLOCK_FOR_SC_FEE_CHECK]
        ] * NUMB_OF_NODES

        # override options for 4th miner node, no limits on sizes so it can mine all tx that are in mempool, no matter
        # what fee or prios they have
        extra_args[NUMB_OF_NODES-1]=['-scproofqueuesize=0', '-logtimemicros=1',
             '-debug=sc', '-debug=py', '-debug=mempool', '-debug=net', '-debug=bench',
             '-blockminsize=%d'%BLK_MIN_SZ,
             '-blocksforscfeecheck=%d'%NUM_BLOCK_FOR_SC_FEE_CHECK]

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 3)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        def flood_mempool():
            mark_logs("Creating many txes...", self.nodes, DEBUG_MODE)
            tot_num_tx = 0
            tot_tx_sz = 0
            taddr_node1 = self.nodes[0].getnewaddress()

            fee = Decimal('0.001')

            # there are a few coinbase utxo now matured
            listunspent = self.nodes[0].listunspent()
            print("num of utxo: ", len(listunspent))

            while True:
                if len(listunspent) <= tot_num_tx:
                    # all utxo have been spent
                    self.sync_all()
                    break

                utxo = listunspent[tot_num_tx]
                change = utxo['amount'] - Decimal(fee)
                raw_inputs  = [ {'txid' : utxo['txid'], 'vout' : utxo['vout']}]
                raw_outs    = { taddr_node1: change }
                try:
                    raw_tx = self.nodes[0].createrawtransaction(raw_inputs, raw_outs)
                    signed_tx = self.nodes[0].signrawtransaction(raw_tx)
                    tx = self.nodes[0].sendrawtransaction(signed_tx['hex'])
                except JSONRPCException as e:
                    errorString = e.error['message']
                    print("Send raw tx failed with reason {}".format(errorString))
                    assert(False)

                tot_num_tx += 1
                hexTx = self.nodes[0].getrawtransaction(tx)
                sz = len(hexTx)//2
                tot_tx_sz += sz

                if tot_tx_sz > 5*EPOCH_LENGTH*BLK_MAX_SZ:
                    self.sync_all()
                    break

            print("tot tx   = {}, tot sz = {} ".format(tot_num_tx, tot_tx_sz))

        def get_sc_fee_min_max_value(scFeesList):
            m_min = Decimal('1000000000.0')
            f_min = Decimal('1000000000.0')
            m_max = Decimal('0.0')
            f_max = Decimal('0.0')
            for i in scFeesList:
                f_min = min(f_min, i['forwardTxScFee'])
                m_min = min(m_min, i['mbtrTxScFee'])
                f_max = max(f_max, i['forwardTxScFee'])
                m_max = max(m_max, i['mbtrTxScFee'])
            return f_min, m_min, f_max, m_max

        '''
        This test checks that FT and McBTR txes are not evicted from mempool until the numbers of blocks 
        set by the constant defined in the base code (and overriden here by the -blocksforscfeecheck zend option)
        are connected to the active chain.
        In order to verify it, two null-fee and low-prio txes are sent to the mempool with a FT and a Mbtr, a large number
        of high-fee/high prio txes are added too, and the miners have a small block capacity, so that the former pair is never 
        mined, while SC epochs increase evolving active certificates. 
        After a node restart, this pattern is repeated but this time the txes are mined before they are evicted from the mempool.
        '''

        FT_SC_FEES=[Decimal('0.001'),   # creation of the SC
                    Decimal('0.002'),   # fwd tx amount
                    Decimal('0.003'),   # cert ep=0, q=1
                    Decimal('0.004'),   # cert ep=0, q=2
                    Decimal('0.005'),   # cert ep=1
                    Decimal('0.006'),   # cert ep=2
                    Decimal('0.007'),   # cert ep=3
                    Decimal('0.008')]   # cert ep=4

        MBTR_SC_FEES=[Decimal('0.011'),  # creation of the SC
                      Decimal('0.011'),  # mbtr tx sc fee, we can also have the same value of creation
                      Decimal('0.033'),  # cert ep=0, q=1
                      Decimal('0.044'),  # cert ep=0, q=2
                      Decimal('0.055'),  # cert ep=1
                      Decimal('0.066'),  # cert ep=2
                      Decimal('0.077'),  # cert ep=3
                      Decimal('0.088')]  # cert ep=4

        # This is a hack for having certs always selected first by miner
        # via the rpc cmd prioritisetransaction
        prio_delta = Decimal(1.0E16)
        fee_delta  = 0 # in zats

        # network topology: (0)--(1)--(2)--(3)

        mark_logs("Node 0 generates 310 blocks", self.nodes, DEBUG_MODE)
        blocks = self.nodes[0].generate(310)
        self.sync_all()

        mark_logs("Node 1 generates 110 blocks", self.nodes, DEBUG_MODE)
        blocks = self.nodes[1].generate(110)
        self.sync_all()

        # Sidechain parameters
        withdrawalEpochLength = EPOCH_LENGTH
        address = "dada"
        creation_amount = Decimal("5.0")
        custom_data = ""
        mbtrRequestDataLength = 1

        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk_tag = "sc1"
        vk = mcTest.generate_params(vk_tag)
        constant = generate_random_field_element_hex()
        cswVk  = ""
        feCfg = []
        bvCfg = []


        # ---------------------------------------------------------------------------------------
        # Node 1 - Create a valid sidechain
        mark_logs("\nNode 1 creates a new sidechain", self.nodes, DEBUG_MODE)

        errorString = ""
        ftScFee   = FT_SC_FEES[0]
        mbtrScFee = MBTR_SC_FEES[0]
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": withdrawalEpochLength,
            "toaddress": address,
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": constant,
            'customData': custom_data,
            'wCeasedVk': cswVk,
            'vFieldElementCertificateFieldConfig': feCfg,
            'vBitVectorCertificateFieldConfig': bvCfg,
            'forwardTransferScFee': ftScFee,
            'mainchainBackwardTransferScFee': mbtrScFee,
            'mainchainBackwardTransferRequestDataLength': mbtrRequestDataLength
        }


        try:
            ret = self.nodes[1].sc_create(cmdInput)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        creating_tx = ret['txid']
        scid = ret['scid']
        mark_logs("tx={}, ftScFee={}, mbtrScFee={}, scid={}".format(creating_tx, ftScFee, mbtrScFee, scid), self.nodes, DEBUG_MODE)
        self.sync_all()

        # send a very small amount to node 2 and 3, which will use it as input
        # for the FT and McBTR, which will have then very low priority
        mark_logs("Node 0 send a small coins to Node2", self.nodes, DEBUG_MODE)
        taddr2 = self.nodes[2].getnewaddress()
        tx_for_input_2 = self.nodes[0].sendtoaddress(taddr2, 0.05)
        mark_logs("tx={}".format(tx_for_input_2), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node 0 send a small coins to Node3", self.nodes, DEBUG_MODE)
        taddr3 = self.nodes[3].getnewaddress()
        tx_for_input_3 = self.nodes[0].sendtoaddress(taddr3, 0.05)
        mark_logs("tx={}".format(tx_for_input_3), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node 1 send a small coins to Node2", self.nodes, DEBUG_MODE)
        taddr2 = self.nodes[2].getnewaddress()
        tx_for_input_2 = self.nodes[1].sendtoaddress(taddr2, 0.05)
        mark_logs("tx={}".format(tx_for_input_2), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node 1 send a small coins to Node3", self.nodes, DEBUG_MODE)
        taddr3 = self.nodes[3].getnewaddress()
        tx_for_input_3 = self.nodes[1].sendtoaddress(taddr3, 0.05)
        mark_logs("tx={}".format(tx_for_input_3), self.nodes, DEBUG_MODE)
        self.sync_all()

        # ---------------------------------------------------------------------------------------
        mark_logs("Node 1 generates " + str(EPOCH_LENGTH) + " blocks", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[1].generate(EPOCH_LENGTH))
        self.sync_all()

        errorString = ""
        ftScFee   = FT_SC_FEES[1]
        mbtrScFee = MBTR_SC_FEES[1]

        # beside having a low priority (see above) these txes also are free: very low probability to get mined
        # if the block size is small and there are other txes
        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 2 creates a tx with a FT output", self.nodes, DEBUG_MODE)

        mc_return_address = self.nodes[2].getnewaddress()
        forwardTransferOuts = [{'toaddress': address, 'amount': ftScFee, "scid":scid, "mcReturnAddress": mc_return_address}]

        try:
            txFT = self.nodes[2].sc_send(forwardTransferOuts, { "fee": 0.0})
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()
        mark_logs("txFT={}, scFee={}".format(txFT, ftScFee), self.nodes, DEBUG_MODE)
        assert_true(txFT in self.nodes[0].getrawmempool())

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 3 creates a tx with an MBTR output", self.nodes, DEBUG_MODE)

        errorString = ""
        fe1 = generate_random_field_element_hex()
        mc_dest_addr1 = self.nodes[3].getnewaddress()
        mbtrOuts = [{'vScRequestData':[fe1], 'scFee':Decimal(mbtrScFee), 'scid':scid, 'mcDestinationAddress':mc_dest_addr1}]

        try:
            txMbtr = self.nodes[3].sc_request_transfer(mbtrOuts, { "fee": 0.0})
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()
        mark_logs("txMbtr={}, scFee={}".format(txMbtr, mbtrScFee), self.nodes, DEBUG_MODE)
        assert_true(txMbtr in self.nodes[0].getrawmempool())

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 creates a new certificate updating FT and McBTR fees", self.nodes, DEBUG_MODE)

        quality = 1
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[1], EPOCH_LENGTH)
        addr_node1 = self.nodes[1].getnewaddress()
        cert_amount = Decimal("1.0")
        amount_cert_1 = [{"address": addr_node1, "amount": cert_amount}]

        ftScFee   = FT_SC_FEES[2]
        mbtrScFee = MBTR_SC_FEES[2]
        scid_swapped = str(swap_bytes(scid))

        proof = mcTest.create_test_proof(
            vk_tag, scid_swapped, epoch_number, quality, mbtrScFee, ftScFee, epoch_cum_tree_hash,
            constant, [addr_node1], [cert_amount])

        cert = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_1, ftScFee, mbtrScFee, CERT_FEE)
        self.sync_all()

        mark_logs("cert={}, epoch={}, ftScFee={}, mbtrScFee={}".format(cert, epoch_number, ftScFee, mbtrScFee), self.nodes, DEBUG_MODE)

        ret = self.nodes[1].prioritisetransaction(cert, prio_delta, fee_delta )
        self.sync_all()

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        bl = self.nodes[1].generate(1)[-1]
        self.sync_all()

        mark_logs("Check cert is in block just mined...", self.nodes, DEBUG_MODE)
        assert_true(cert in self.nodes[0].getblock(bl, True)['cert'])
        mark_logs("Check txes are not in block just mined...", self.nodes, DEBUG_MODE)
        assert_false(txFT in self.nodes[0].getblock(bl, True)['tx'])
        assert_false(txMbtr in self.nodes[0].getblock(bl, True)['tx'])
        assert_true(txFT in self.nodes[0].getrawmempool(True))
        assert_true(txMbtr in self.nodes[0].getrawmempool(True))

        scFeesList = self.nodes[0].getscinfo(scid)['items'][0]['scFees']
        f_min, m_min, f_max, m_max = get_sc_fee_min_max_value(scFeesList)
        assert_equal(f_min, FT_SC_FEES[0])
        assert_equal(m_min, MBTR_SC_FEES[0])
        assert_equal(f_max, FT_SC_FEES[0])
        assert_equal(m_max, MBTR_SC_FEES[0])


        mark_logs("\nNode 0 creates a second certificate of higher quality updating SC fees", self.nodes, DEBUG_MODE)

        quality += 1
        ftScFee   = FT_SC_FEES[3]
        mbtrScFee = MBTR_SC_FEES[3]

        proof = mcTest.create_test_proof(
            vk_tag, scid_swapped, epoch_number, quality, mbtrScFee, ftScFee, epoch_cum_tree_hash,
            constant, [addr_node1], [cert_amount])

        cert = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_1, ftScFee, mbtrScFee, CERT_FEE)
        self.sync_all()

        mark_logs("cert={}, epoch={}, ftScFee={}, mbtrScFee={}".format(cert, epoch_number, ftScFee, mbtrScFee), self.nodes, DEBUG_MODE)

        ret = self.nodes[1].prioritisetransaction(cert, prio_delta, fee_delta )
        self.sync_all()

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        bl = self.nodes[1].generate(1)[-1]
        self.sync_all()
        bl_json = self.nodes[0].getblock(bl, True)
        mark_logs("Check cert is in block just mined...", self.nodes, DEBUG_MODE)
        assert_true(cert in bl_json['cert'])
        mark_logs("Check txes are not in block just mined...", self.nodes, DEBUG_MODE)
        assert_false(txFT in bl_json['tx'])
        assert_false(txMbtr in bl_json['tx'])
        assert_true(txFT in self.nodes[0].getrawmempool(True))
        assert_true(txMbtr in self.nodes[0].getrawmempool(True))

        assert_equal(scFeesList, self.nodes[0].getscinfo(scid)['items'][0]['scFees'])

        #------------------------------------------------------------------------------------------------
        # flood the mempool with non-free and hi-prio txes so that next blocks (with small max size) will
        # not include FT and McBTR, which are free and with a low priority
        flood_mempool()
        self.sync_all()

        # advance two epochs
        mark_logs("\nLet 2 epochs pass by...", self.nodes, DEBUG_MODE)

        mark_logs("Node 1 generates " + str(EPOCH_LENGTH-2) + " blocks", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(EPOCH_LENGTH-2)
        self.sync_all()

        ftScFee   = FT_SC_FEES[4]
        mbtrScFee = MBTR_SC_FEES[4]

        cert, epoch_number = advance_epoch(
            mcTest, self.nodes[1], self.sync_all, scid, "sc1", constant, EPOCH_LENGTH,
            quality, CERT_FEE, ftScFee, mbtrScFee, generateNumBlocks=0)

        self.sync_all()

        mark_logs("cert={}, epoch={}, ftScFee={}, mbtrScFee={}".format(cert, epoch_number, ftScFee, mbtrScFee), self.nodes, DEBUG_MODE)
        assert_true(cert in self.nodes[1].getrawmempool(True))

        ret = self.nodes[1].prioritisetransaction(cert, prio_delta, fee_delta )
        self.sync_all()

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        scFeesList = self.nodes[0].getscinfo(scid)['items'][0]['scFees']
        f_min, m_min, f_max, m_max = get_sc_fee_min_max_value(scFeesList)
        assert_equal(f_min, FT_SC_FEES[0])
        assert_equal(m_min, MBTR_SC_FEES[0])
        assert_equal(f_max, FT_SC_FEES[3])
        assert_equal(m_max, MBTR_SC_FEES[3])

        mark_logs("Check txes are still in mempool...", self.nodes, DEBUG_MODE)
        assert_true(txFT in self.nodes[0].getrawmempool(True))
        assert_true(txMbtr in self.nodes[0].getrawmempool(True))


        ftScFee   = FT_SC_FEES[5]
        mbtrScFee = MBTR_SC_FEES[5]
        cert, epoch_number = advance_epoch(
            mcTest, self.nodes[1], self.sync_all, scid, "sc1", constant, EPOCH_LENGTH,
            quality, CERT_FEE, ftScFee, mbtrScFee, generateNumBlocks=(EPOCH_LENGTH-1))

        mark_logs("cert={}, epoch={}, ftScFee={}, mbtrScFee={}".format(cert, epoch_number, ftScFee, mbtrScFee), self.nodes, DEBUG_MODE)
        assert_true(txFT in self.nodes[1].getrawmempool(True))

        mark_logs("Check txes are still in mempool...", self.nodes, DEBUG_MODE)
        assert_true(txFT in self.nodes[0].getrawmempool(True))
        assert_true(txMbtr in self.nodes[0].getrawmempool(True))

        ret = self.nodes[0].prioritisetransaction(cert, prio_delta, fee_delta )
        self.sync_all()

        mark_logs("Node 0 generates 1 block", self.nodes, DEBUG_MODE)
        bl = self.nodes[0].generate(1)[-1]
        self.sync_all()

        scFeesList = self.nodes[0].getscinfo(scid)['items'][0]['scFees']
        f_min, m_min, f_max, m_max = get_sc_fee_min_max_value(scFeesList)
        assert_equal(f_min, FT_SC_FEES[3])
        assert_equal(m_min, MBTR_SC_FEES[3])
        assert_equal(f_max, FT_SC_FEES[4])
        assert_equal(m_max, MBTR_SC_FEES[4])

        mark_logs("Check txes are no more in mempool...", self.nodes, DEBUG_MODE)
        assert_false(txFT in self.nodes[0].getrawmempool(True))
        assert_false(txMbtr in self.nodes[0].getrawmempool(True))

        mark_logs("Check txes have neither been mined...", self.nodes, DEBUG_MODE)
        assert_false(txFT in self.nodes[0].getblock(bl, True)['tx'])
        assert_false(txMbtr in self.nodes[0].getblock(bl, True)['tx'])

        # this is for clearing mempool, otherwise the restart is blocked because txes are not rebroadcasted
        mark_logs("Node 3 generates 1 block", self.nodes, DEBUG_MODE)
        bl = self.nodes[3].generate(1)[-1]
        self.sync_all()

        mark_logs("Checking persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        bbh = self.nodes[0].getbestblockhash()
        assert_equal(bl, bbh)

        scFeesList2 = self.nodes[0].getscinfo(scid)['items'][0]['scFees']
        assert_equal(scFeesList, scFeesList2)

        ftScFee   = FT_SC_FEES[6]
        mbtrScFee = MBTR_SC_FEES[5]
        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 2 creates a second tx with a FT output scFee={}".format(ftScFee), self.nodes, DEBUG_MODE)

        forwardTransferOuts = [{'toaddress': address, 'amount': ftScFee, "scid":scid, "mcReturnAddress": mc_return_address}]

        try:
            txFT = self.nodes[2].sc_send(forwardTransferOuts, { "fee": 0.0})
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()
        mark_logs("txFT={}, scFee={}".format(txFT, ftScFee), self.nodes, DEBUG_MODE)
        assert_true(txFT in self.nodes[0].getrawmempool())

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 3 creates a second tx with an MBTR output, scFee={}".format(mbtrScFee), self.nodes, DEBUG_MODE)

        errorString = ""
        fe1 = generate_random_field_element_hex()
        mc_dest_addr1 = self.nodes[3].getnewaddress()
        mbtrOuts = [{'vScRequestData': [fe1], 'scFee': Decimal(mbtrScFee), 'scid': scid, 'mcDestinationAddress': mc_dest_addr1}]

        try:
            txMbtr = self.nodes[3].sc_request_transfer(mbtrOuts, { "fee": 0.0})
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()
        mark_logs("txMbtr={}, scFee={}".format(txMbtr, mbtrScFee), self.nodes, DEBUG_MODE)
        assert_true(txMbtr in self.nodes[0].getrawmempool())


        #------------------------------------------------------------------------------------------------
        # flood the mempool with non-free and hi-prio txes so that next blocks (with small max size) will
        # not include FT and McBTR, which are free and with a low priority
        flood_mempool()
        self.sync_all()

        ftScFee   = FT_SC_FEES[6]
        mbtrScFee = MBTR_SC_FEES[6]

        cert, epoch_number = advance_epoch(
            mcTest, self.nodes[1], self.sync_all, scid, "sc1", constant, EPOCH_LENGTH,
            quality, CERT_FEE, ftScFee, mbtrScFee, generateNumBlocks=(EPOCH_LENGTH-1))

        self.sync_all()

        mark_logs("cert={}, epoch={}, ftScFee={}, mbtrScFee={}".format(cert, epoch_number, ftScFee, mbtrScFee), self.nodes, DEBUG_MODE)
        assert_true(cert in self.nodes[1].getrawmempool(True))

        ret = self.nodes[1].prioritisetransaction(cert, prio_delta, fee_delta )
        self.sync_all()

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        scFeesList = self.nodes[0].getscinfo(scid)['items'][0]['scFees']
        f_min, m_min, f_max, m_max = get_sc_fee_min_max_value(scFeesList)
        assert_equal(f_min, FT_SC_FEES[4])
        assert_equal(m_min, MBTR_SC_FEES[4])
        assert_equal(f_max, FT_SC_FEES[5])
        assert_equal(m_max, MBTR_SC_FEES[5])

        mark_logs("Check txes are still in mempool...", self.nodes, DEBUG_MODE)
        assert_true(txFT in self.nodes[0].getrawmempool(True))
        assert_true(txMbtr in self.nodes[0].getrawmempool(True))

        ftScFee   = FT_SC_FEES[7]
        mbtrScFee = MBTR_SC_FEES[7]
        cert, epoch_number = advance_epoch(
            mcTest, self.nodes[1], self.sync_all, scid, "sc1", constant, EPOCH_LENGTH,
            quality, CERT_FEE, ftScFee, mbtrScFee, generateNumBlocks=(EPOCH_LENGTH-1))

        mark_logs("cert={}, epoch={}, ftScFee={}, mbtrScFee={}".format(cert, epoch_number, ftScFee, mbtrScFee), self.nodes, DEBUG_MODE)
        assert_true(txFT in self.nodes[1].getrawmempool(True))

        mark_logs("Check txes are still in mempool...", self.nodes, DEBUG_MODE)
        assert_true(txFT in self.nodes[0].getrawmempool(True))
        assert_true(txMbtr in self.nodes[0].getrawmempool(True))

        ret = self.nodes[3].prioritisetransaction(cert, prio_delta, fee_delta )
        self.sync_all()

        mark_logs("Node 3 generates 1 block", self.nodes, DEBUG_MODE)
        bl = self.nodes[3].generate(1)[-1]
        self.sync_all()

        scFeesList = self.nodes[0].getscinfo(scid)['items'][0]['scFees']
        f_min, m_min, f_max, m_max = get_sc_fee_min_max_value(scFeesList)
        assert_equal(f_min, FT_SC_FEES[5])
        assert_equal(m_min, MBTR_SC_FEES[5])
        assert_equal(f_max, FT_SC_FEES[6])
        assert_equal(m_max, MBTR_SC_FEES[6])

        mark_logs("Check txes have been mined...", self.nodes, DEBUG_MODE)
        assert_true(txFT in self.nodes[0].getblock(bl, True)['tx'])
        assert_true(txMbtr in self.nodes[0].getblock(bl, True)['tx'])




if __name__ == '__main__':
    SCStaleFtAndMbtrTest().main()
