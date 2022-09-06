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
from test_framework.mc_test.mc_test import CertTestUtils, CSWTestUtils, generate_random_field_element_hex
import os
from decimal import Decimal
import pprint
import time

NUMB_OF_NODES = 3
DEBUG_MODE = 1
EPOCH_LENGTH = 10
MAX_NUM_OF_CSW_INPUTS_PER_SC = 5 

class ScCswEvictionFromMempool(BitcoinTestFramework):
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
            ]
        ] * NUMB_OF_NODES

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        Create 2 SCs and let them cease after 2 epochs. Create a tx with 5 csw inputs for each of the SCs and send it over the network.
        Verify that it is accepted in the mempool and that another tx with a further csw input is not accepted due to mempool limit reached.
        Mine a block that includes the tx in the mempool and verify that resending the previously rejected tx this time is succesful.
        Mine another block and then disconnect both last blocks from active chain in all nodes.
        Verify that only the last tx is included in the mempool, not the first (big) one.
        '''

        # network topology: (0)--(1)--(2)

        mark_logs("Node 0 generates 310 blocks", self.nodes, DEBUG_MODE)
        blocks = self.nodes[0].generate(310)
        self.sync_all()

        mark_logs("Node 1 generates 110 blocks", self.nodes, DEBUG_MODE)
        blocks = self.nodes[1].generate(110)
        self.sync_all()

        # Sidechain parameters
        withdrawalEpochLength = EPOCH_LENGTH
        address = "dada"
        creation_amount = Decimal("50.0")
        custom_data = ""
        mbtrRequestDataLength = 1

        certMcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        cswMcTest = CSWTestUtils(self.options.tmpdir, self.options.srcdir)

        # generate wCertVk and constant
        certVk1 = certMcTest.generate_params("sc1")
        certVk2 = certMcTest.generate_params("sc2")
        cswVk1 = cswMcTest.generate_params("sc1")
        cswVk2 = cswMcTest.generate_params("sc2")
        constant1 = generate_random_field_element_hex()
        constant2 = generate_random_field_element_hex()

        feCfg = []
        bvCfg = []

        ftScFee   = Decimal('0.01')
        mbtrScFee = Decimal('0.02')

        # ---------------------------------------------------------------------------------------
        # Node 1 - Create a valid sidechain
        mark_logs("\nNode 0 creates 2 sidechains", self.nodes, DEBUG_MODE)
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": withdrawalEpochLength,
            "toaddress": address,
            "amount": creation_amount,
            "wCertVk": certVk1,
            "constant": constant1,
            'customData': custom_data,
            'wCeasedVk': cswVk1,
            'vFieldElementCertificateFieldConfig': feCfg,
            'vBitVectorCertificateFieldConfig': bvCfg,
            'forwardTransferScFee': ftScFee,
            'mainchainBackwardTransferScFee': mbtrScFee,
            'mainchainBackwardTransferRequestDataLength': mbtrRequestDataLength
        }

        try:
            ret = self.nodes[0].sc_create(cmdInput)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        scid1 = ret['scid']
        mark_logs("scid1={}".format(scid1), self.nodes, DEBUG_MODE)
        self.sync_all()
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": withdrawalEpochLength,
            "toaddress": address,
            "amount": creation_amount,
            "wCertVk": certVk2,
            "constant": constant2,
            'customData': custom_data,
            'wCeasedVk': cswVk2,
            'vFieldElementCertificateFieldConfig': feCfg,
            'vBitVectorCertificateFieldConfig': bvCfg,
            'forwardTransferScFee': ftScFee,
            'mainchainBackwardTransferScFee': mbtrScFee,
            'mainchainBackwardTransferRequestDataLength': mbtrRequestDataLength
        }

        try:
            ret = self.nodes[0].sc_create(cmdInput)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        scid2 = ret['scid']
        mark_logs("scid2={}".format(scid2), self.nodes, DEBUG_MODE)
        self.sync_all()

        # advance two epochs
        mark_logs("\nLet 2 epochs pass by...", self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
            scid1, "sc1", constant1, EPOCH_LENGTH)

        mark_logs("\n==> certificate for SC1 epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
            scid2, "sc2", constant2, EPOCH_LENGTH, generateNumBlocks=0) # do not generate

        mark_logs("\n==> certificate for SC2 epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
             scid1, "sc1", constant1, EPOCH_LENGTH)

        mark_logs("\n==> certificate for SC1 epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
             scid2, "sc2", constant2, EPOCH_LENGTH, generateNumBlocks=0) # do not generate

        mark_logs("\n==> certificate for SC2 epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)


        mark_logs("Let both SCs cease... ", self.nodes, DEBUG_MODE)

        nbl = int(EPOCH_LENGTH * 1.5)
        mark_logs("Node0 generates {} blocks".format(nbl), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(nbl)
        self.sync_all()

        # check they are really ceased
        ret = self.nodes[0].getscinfo(scid1, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")
        ret = self.nodes[0].getscinfo(scid2, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")

        # create a tx with 5 CSW for sc1 and 5 CSW for sc2
        mark_logs("\nCreate {} CSWs for each of the 2 SCs in a tx... ".format(MAX_NUM_OF_CSW_INPUTS_PER_SC), self.nodes, DEBUG_MODE)

        # the csw total amount must be lesser than sc balances
        sc_csw_amount = (creation_amount/(MAX_NUM_OF_CSW_INPUTS_PER_SC*4))

        # CSW sender MC address, in taddress and pub key hash formats
        csw_mc_address = self.nodes[0].getnewaddress()

        nullifiers1 = []
        nullifiers2 = []
        csw_proofs1 = []
        csw_proofs2 = []

        actCertData1 = self.nodes[0].getactivecertdatahash(scid1)['certDataHash']
        actCertData2 = self.nodes[0].getactivecertdatahash(scid2)['certDataHash']

        ceasingCumScTxCommTree1 = self.nodes[0].getceasingcumsccommtreehash(scid1)['ceasingCumScTxCommTree']
        ceasingCumScTxCommTree2 = self.nodes[0].getceasingcumsccommtreehash(scid2)['ceasingCumScTxCommTree']

        scid1_swapped = swap_bytes(scid1)
        scid2_swapped = swap_bytes(scid2)

        for i in range(MAX_NUM_OF_CSW_INPUTS_PER_SC + 1):

            nullifiers1.append(generate_random_field_element_hex())
            nullifiers2.append(generate_random_field_element_hex())

            csw_proofs1.append(cswMcTest.create_test_proof(
                "sc1", sc_csw_amount, str(scid1_swapped), nullifiers1[i], csw_mc_address,
                ceasingCumScTxCommTree1, cert_data_hash = actCertData1, constant = constant1))

            csw_proofs2.append(cswMcTest.create_test_proof(
                "sc2", sc_csw_amount, str(scid2_swapped), nullifiers2[i], csw_mc_address,
                ceasingCumScTxCommTree2, cert_data_hash = actCertData2, constant = constant2))
        
        sc_csws = []

        for i in range(MAX_NUM_OF_CSW_INPUTS_PER_SC):
            sc_csws.append(
            {
                "amount": sc_csw_amount,
                "senderAddress": csw_mc_address,
                "scId": scid1,
                "epoch": 0,
                "nullifier": nullifiers1[i],
                "activeCertData": actCertData1,
                "ceasingCumScTxCommTree": ceasingCumScTxCommTree1,
                "scProof": csw_proofs1[i]
            })
            sc_csws.append(
            {
                "amount": sc_csw_amount,
                "senderAddress": csw_mc_address,
                "scId": scid2,
                "epoch": 0,
                "nullifier": nullifiers2[i],
                "activeCertData": actCertData2,
                "ceasingCumScTxCommTree": ceasingCumScTxCommTree2,
                "scProof": csw_proofs2[i]
            })

        # recipient MC address, the same for all csw
        taddr_2 = self.nodes[2].getnewaddress()
        sc_csw_tx_outs = {taddr_2: Decimal(sc_csw_amount*10)}

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        finalRawtx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        mark_logs("sent csw tx1 {}".format(finalRawtx), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Check tx is in mempool...", self.nodes, DEBUG_MODE)
        assert_true(finalRawtx in self.nodes[2].getrawmempool())

        # send another csw input for scid1 and verify it is rejected
        sc_csw = [{
            "amount": sc_csw_amount,
            "senderAddress": csw_mc_address,
            "scId": scid1,
            "epoch": 0,
            "nullifier": nullifiers1[-1],
            "activeCertData": actCertData1,
            "ceasingCumScTxCommTree": ceasingCumScTxCommTree1,
            "scProof": csw_proofs1[-1]
        }]

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csw)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        mark_logs("Sending a further csw input to scid1, expecting failure...", self.nodes, DEBUG_MODE)
        try:
            finalRawtx2 = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send csw failed (expected) with reason {}".format(errorString), self.nodes, DEBUG_MODE)

        # mine a block, in this way mempool is cleared and we can re-send the rejected tx
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        inv_hash = self.nodes[0].getbestblockhash()

        mark_logs("Re-sending previously rejected tx with one more csw...", self.nodes, DEBUG_MODE)
        try:
            finalRawtx2 = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
            mark_logs("sent csw tx {}".format(finalRawtx), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send csw failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)
        self.sync_all()

        mark_logs("Check tx is in mempool...", self.nodes, DEBUG_MODE)
        assert_true(finalRawtx2 in self.nodes[2].getrawmempool())

        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        # disconnect previous two blocks and verify that only 5 txes are in the mempool (not 6)
        mark_logs("Disconnecting last 2 blocks", self.nodes, DEBUG_MODE)
        self.nodes[0].invalidateblock(inv_hash)
        self.nodes[1].invalidateblock(inv_hash)
        self.nodes[2].invalidateblock(inv_hash)
        self.sync_all()
        
        mark_logs("Check tx2 is in mempool but not tx1...", self.nodes, DEBUG_MODE)
        assert_true(finalRawtx2 in self.nodes[0].getrawmempool())
        assert_false(finalRawtx in self.nodes[1].getrawmempool())


if __name__ == '__main__':
    ScCswEvictionFromMempool().main()
