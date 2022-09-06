#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, initialize_chain_clean, \
    start_nodes, connect_nodes_bi, mark_logs, \
    get_epoch_data, get_spendable, swap_bytes, advance_epoch, \
    get_field_element_with_padding
from test_framework.test_framework import ForkHeights
from test_framework.mc_test.mc_test import CSWTestUtils, CertTestUtils, generate_random_field_element_hex
import os
from decimal import Decimal
import subprocess

NUMB_OF_NODES = 2
DEBUG_MODE = 1
EPOCH_LENGTH = 5
FT_SC_FEE = Decimal('0.005')
MBTR_SC_FEE = Decimal('0.006')
CERT_FEE = Decimal("0.000123")

BIT_VECTOR_BUF = "021f8b08000000000002ff017f0080ff44c7e21ba1c7c0a29de006cb8074e2ba39f15abfef2525a4cbb3f235734410bda21cdab6624de769ceec818ac6c2d3a01e382e357dce1f6e9a0ff281f0fedae0efe274351db37599af457984dcf8e3ae4479e0561341adfff4746fbe274d90f6f76b8a2552a6ebb98aee918c7ceac058f4c1ae0131249546ef5e22f4187a07da02ca5b7f000000"
BIT_VECTOR_FE  = "8a7d5229f440d4700d8b0343de4e14400d1cb87428abf83bd67153bf58871721"

class scRpcCmdsJsonOutput(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[['-logtimemicros=1', '-debug=sc', '-scproofqueuesize=0', 
                                              '-debug=py', '-debug=mempool', '-debug=net', '-debug=cert',
                                              '-debug=bench', '-txindex=1']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        This script is useful for the generation of json outputs to be stored in github:
        (https://github.com/HorizenOfficial/zen/tree/master/doc/json-examples)
        ---
        System calls to the zen-cli are used (instead for instance of json.dump()) in order to preserve the exact order 
        of the JSON keys as the one the user gets when sending rpc commands on the console.
        ---
        In order to enable the writing of the cmds output to a file please set the constant WRITE_OUTPUT_TO_FILE to 'True'
        and if needed, set the preferred path where to write to, in the constant JSON_FILES_FOLDER_PATH
        '''
        WRITE_OUTPUT_TO_FILE = False
        JSON_FILES_FOLDER_PATH = "../../doc/json-examples/"


        def _get_path_info(nodeid, fileName):
            node_data_dir = os.path.join(self.options.tmpdir, "node"+str(nodeid))
            node_conf_dir = os.path.join(node_data_dir, "zen.conf")
            file_with_path = os.path.join(JSON_FILES_FOLDER_PATH + fileName)
            return node_conf_dir, file_with_path
 

        def dump_json_tx(fileName, tx, nodeid=0):
         
            if WRITE_OUTPUT_TO_FILE == False:
                return

            node_conf_dir, file_with_path = _get_path_info(nodeid, fileName)

            hex_tx = self.nodes[nodeid].getrawtransaction(tx)
            cmd_ret = subprocess.check_output([ os.getenv("BITCOINCLI", "zen-cli"), "-conf="+ node_conf_dir,
                                    "-rpcwait", "decoderawtransaction", str(hex_tx).rstrip()])
            with open(file_with_path, 'w') as f:
                f.write(cmd_ret)

        def dump_json_block(fileName, blockhash, verbose=2, nodeid=0):
            if WRITE_OUTPUT_TO_FILE == False:
                return

            node_conf_dir, file_with_path = _get_path_info(nodeid, fileName)

            cmd_ret = subprocess.check_output([ os.getenv("BITCOINCLI", "zen-cli"), "-conf="+ node_conf_dir,
                                    "-rpcwait", "getblock", blockhash, str(verbose)])
            with open(file_with_path, 'w') as f:
                f.write(cmd_ret)

        def dump_json_getscinfo(fileName, nodeid=0):
            if WRITE_OUTPUT_TO_FILE == False:
                return

            node_conf_dir, file_with_path = _get_path_info(nodeid, fileName)
 
            cmd_ret = subprocess.check_output([ os.getenv("BITCOINCLI", "zen-cli"), "-conf="+ node_conf_dir,
                                    "-rpcwait", "getscinfo", "*"])
            with open(file_with_path, 'w') as f:
                f.write(cmd_ret)

        def dump_json_getblocktemplate(fileName, nodeid=0):
            if WRITE_OUTPUT_TO_FILE == False:
                return

            node_conf_dir, file_with_path = _get_path_info(nodeid, fileName)
 
            cmd_ret = subprocess.check_output([ os.getenv("BITCOINCLI", "zen-cli"), "-conf="+ node_conf_dir,
                                    "-rpcwait", "getblocktemplate"])
            with open(file_with_path, 'w') as f:
                f.write(cmd_ret)


        # network topology: (0)--(1)

        mark_logs("Node 1 generates 2 block",self.nodes,DEBUG_MODE)
        self.nodes[1].generate(2)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()

        #generate wCertVk and constant
        certMcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        cswMcTest = CSWTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = certMcTest.generate_params('sc1')
        cswVk = cswMcTest.generate_params("sc1")
        constant1 = generate_random_field_element_hex()

        amount = Decimal('10.0')
        fee = Decimal('0.000025')
        feCfg = []
        cmtCfg = []

        # all certs must have custom FieldElements with exactly those values as size in bits 
        feCfg.append([31, 48, 16])

        # one custom bv element with:
        # - as many bits in the uncompressed form (must be divisible by 254 and 8)
        # - a compressed size that allows the usage of BIT_VECTOR_BUF
        cmtCfg.append([[254*4, len(BIT_VECTOR_BUF)//2]])

        # ascii chars, just for storing a text string
        customData = "746869732069732061207465737420737472696e67"

        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': EPOCH_LENGTH,
            'amount': amount,
            'fee': fee,
            'constant':constant1,
            'wCertVk': vk,
            'toaddress':"cdcd",
            'wCeasedVk': cswVk,
            'customData': customData,
            'vFieldElementCertificateFieldConfig': feCfg[0],
            'vBitVectorCertificateFieldConfig': cmtCfg[0],
            'forwardTransferScFee': Decimal('0.001'),
            'mainchainBackwardTransferScFee' : Decimal('0.002'),
            'mainchainBackwardTransferRequestDataLength': 2
        }

        mark_logs("\nNode 1 create SC1 with valid vFieldElementCertificateFieldConfig / vBitVectorCertificateFieldConfig pair", self.nodes,DEBUG_MODE)
        try:
            res = self.nodes[1].sc_create(cmdInput)
            tx =   res['txid']
            scid1 = res['scid']
            scid1_swapped = str(swap_bytes(scid1))
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()
        print("tx = {}".format(tx))

        dump_json_tx('sidechain-creation-output.json', tx)


        # two more SC creations
        #-------------------------------------------------------
        vk = certMcTest.generate_params("sc2")
        constant2 = generate_random_field_element_hex()
        customData = "c0ffee"
        cswVk  = ""
        feCfg.append([16])
        cmtCfg.append([])

        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': EPOCH_LENGTH,
            'toaddress': "dada",
            'amount': amount,
            'wCertVk': vk,
            'customData': customData,
            'constant': constant2,
            'wCeasedVk': cswVk,
            'vFieldElementCertificateFieldConfig': feCfg[1],
            'vBitVectorCertificateFieldConfig': cmtCfg[1],
            'forwardTransferScFee': 0,
            'mainchainBackwardTransferScFee': 0,
            'mainchainBackwardTransferRequestDataLength': 1}

        mark_logs("\nNode 1 create SC2 with valid vFieldElementCertificateFieldConfig / vBitVectorCertificateFieldConfig pair", self.nodes,DEBUG_MODE)
        try:
            ret = self.nodes[1].sc_create(cmdInput)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)
        self.sync_all()
 
        creating_tx = ret['txid']
        scid2 = ret['scid']
        scid2_swapped = str(swap_bytes(scid2))

        print("tx = {}".format(creating_tx))

        decoded_tx = self.nodes[1].getrawtransaction(creating_tx, 1)
        dec_sc_id = decoded_tx['vsc_ccout'][0]['scid']

        #-------------------------------------------------------
        vk = certMcTest.generate_params("sc3")
        constant3 = generate_random_field_element_hex()
        customData = "badc0ffee"
        feCfg.append([])
        cmtCfg.append([[254*8*4, 1967]])

        sc_cr = [{
            "version": 0,
            "epoch_length": EPOCH_LENGTH,
            "amount":amount,
            "address":"ddaa",
            "wCertVk": vk,
            "constant": constant3,
            "vFieldElementCertificateFieldConfig":feCfg[2],
            "vBitVectorCertificateFieldConfig":cmtCfg[2]
        }]

        mark_logs("\nNode 0 create SC3 with valid vFieldElementCertificateFieldConfig / vBitVectorCertificateFieldConfig pair", self.nodes,DEBUG_MODE)
        try:
            rawtx=self.nodes[0].createrawtransaction([],{},[],sc_cr)
            funded_tx = self.nodes[0].fundrawtransaction(rawtx)
            sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'])
            creating_tx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)
        self.sync_all()
 
        decoded_tx = self.nodes[0].getrawtransaction(creating_tx, 1)
        scid3 = decoded_tx['vsc_ccout'][0]['scid']
        ("tx = {}".format(creating_tx))

        #-------------------------------------------------------
        mark_logs("\nNode 0 generates 1 block confirming SC creations", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # send funds to SC1
        amounts = []
        fwt_amount_1 = Decimal("2.0")
        fwt_amount_2 = Decimal("10.0")
        fwt_amount_3 = Decimal("3.0")
        fwt_amount_many = fwt_amount_1 + fwt_amount_2 + fwt_amount_3

        mark_logs("\nNode 0 sends 3 amounts to SC 1 (tot: " + str(fwt_amount_many) + ")", self.nodes, DEBUG_MODE)
        mc_return_address1 = self.nodes[0].getnewaddress()
        mc_return_address2 = self.nodes[0].getnewaddress()
        mc_return_address3 = self.nodes[0].getnewaddress()
        amounts.append({"toaddress": "add1", "amount": fwt_amount_1, "scid": scid1, "mcReturnAddress": mc_return_address1})
        amounts.append({"toaddress": "add2", "amount": fwt_amount_2, "scid": scid2, "mcReturnAddress": mc_return_address2})
        amounts.append({"toaddress": "add3", "amount": fwt_amount_3, "scid": scid3, "mcReturnAddress": mc_return_address3})
        tx = self.nodes[0].sc_send(amounts)
        self.sync_all()

        print("tx = {}".format(tx))
        dump_json_tx('forward-transfer-output.json', tx)

        # request some mainchain backward transfer
        mark_logs("\nNode0 creates a tx with a bwt request", self.nodes, DEBUG_MODE)
        fe1 = generate_random_field_element_hex()
        fe2 = generate_random_field_element_hex()
        fe3 = generate_random_field_element_hex()
        mc_dest_addr0 = self.nodes[0].getnewaddress()
        mc_dest_addr1 = self.nodes[1].getnewaddress()
        outputs = [
            {'vScRequestData': [fe1, fe2], 'scFee':Decimal("0.0025"), 'scid':scid1, 'mcDestinationAddress': mc_dest_addr0},
            {'vScRequestData': [fe3], 'scFee':Decimal("0.0026"), 'scid':scid2, 'mcDestinationAddress': mc_dest_addr1}
        ]

        cmdParms = { "minconf":0, "changeaddress":mc_dest_addr0 }
        try:
            tx = self.nodes[0].sc_request_transfer(outputs, cmdParms)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)
        print("tx = {}".format(tx))
        dump_json_tx('mainchain-backward-transfer-request.json', tx)

        #-------------------------------------------------------
        # advance epoch
        mark_logs("\nNode 0 generates {} block".format(EPOCH_LENGTH-1), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(EPOCH_LENGTH - 1)
        self.sync_all()

        epoch_number_1, epoch_cum_tree_hash_1, _ = get_epoch_data(scid1, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number_1, epoch_cum_tree_hash_1), self.nodes, DEBUG_MODE)

        addr_node1a = self.nodes[1].getnewaddress()
        addr_node1b = self.nodes[1].getnewaddress()
        addr_node0 = self.nodes[1].getnewaddress()
        bwt_amount_1 = Decimal("0.2")
        bwt_amount_2 = Decimal("0.15")

        # get a UTXO
        utx, change = get_spendable(self.nodes[0], CERT_FEE)

        inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        outputs = { addr_node0 : change }
        bwt_outs = [
            {"address": addr_node1a, "amount": bwt_amount_1},
            {"address": addr_node1b, "amount": bwt_amount_2}
        ]
        addresses = []
        amounts = []

        # preserve order for proof validity
        for entry in bwt_outs:
            addresses.append(entry["address"])
            amounts.append(entry["amount"])

        #-------------------------------------------------------
        mark_logs("\nCreate raw cert with good custom field elements for SC2...", self.nodes, DEBUG_MODE)
        # cfgs for SC2: [16], []
        # we must be careful with ending bits for having valid fe.
        vCfe = ["0100"]
        vCmt = []

        # serialized fe for the proof has 32 byte size
        fe1 = get_field_element_with_padding("0100", 0)

        quality = 72
        scProof3 = certMcTest.create_test_proof(
            'sc2', scid2_swapped, epoch_number_1, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash_1,
            constant = constant2, pks = addresses, amounts = amounts, custom_fields = [fe1])

        params = {
            'scid': scid2,
            'quality': quality,
            'endEpochCumScTxCommTreeRoot': epoch_cum_tree_hash_1,
            'scProof': scProof3,
            'withdrawalEpochNumber': epoch_number_1,
            'vFieldElementCertificateField': vCfe,
            'vBitVectorCertificateField':vCmt,
            'ftScFee':FT_SC_FEE,
            'mbtrScFee':MBTR_SC_FEE
        }

        try:
            rawcert = self.nodes[0].createrawcertificate(inputs, outputs, bwt_outs, params)
            signed_cert = self.nodes[0].signrawtransaction(rawcert)
            cert = self.nodes[0].sendrawtransaction(signed_cert['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        self.sync_all()
        print("cert = {}".format(cert))

        #-------------------------------------------------------
        # get another UTXO
        utx, change = get_spendable(self.nodes[0], CERT_FEE)
        inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
        outputs = { self.nodes[0].getnewaddress() : change }

        mark_logs("\nCreate raw cert with good custom field elements for SC1...", self.nodes, DEBUG_MODE)

        # Any number ending with 0x00 is not over module for being a valid field element, therefore it is OK
        vCfe = ["ab000100", "ccccdddd0000", "0100"]
        # this is a compressed buffer which will yield a valid field element for the proof (see below)
        vCmt = [BIT_VECTOR_BUF]

        fe1 = get_field_element_with_padding("ab000100", 0)
        fe2 = get_field_element_with_padding("ccccdddd0000", 0)
        fe3 = get_field_element_with_padding("0100", 0)
        fe4 = BIT_VECTOR_FE

        quality = 18
        scProof3 = certMcTest.create_test_proof(
            'sc1', scid1_swapped, epoch_number_1, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash_1,
            constant = constant1, pks = addresses, amounts = amounts, custom_fields = [fe1, fe2, fe3, fe4])

        params = {
            'scid': scid1,
            'quality': quality,
            'endEpochCumScTxCommTreeRoot': epoch_cum_tree_hash_1,
            'scProof': scProof3,
            'withdrawalEpochNumber': epoch_number_1,
            'vFieldElementCertificateField': vCfe,
            'vBitVectorCertificateField': vCmt,
            'ftScFee': FT_SC_FEE,
            'mbtrScFee': MBTR_SC_FEE
        }

        try:
            rawcert = self.nodes[0].createrawcertificate(inputs, outputs, bwt_outs, params)
            signed_cert = self.nodes[0].signrawtransaction(rawcert)
            cert = self.nodes[0].sendrawtransaction(signed_cert['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)
        self.sync_all()

        print("cert = {}".format(cert))
        dump_json_tx('certificate-with-backward-transfer.json', cert)

        # add a pair of standard txes
        self.nodes[0].sendtoaddress(addr_node1a, Decimal('0.1'))
        self.nodes[1].sendtoaddress(addr_node0, Decimal('0.2'))

        dump_json_getblocktemplate('getblocktemplate.json', nodeid=0)


        bl = self.nodes[0].generate(1)[-1]
        self.sync_all()

        dump_json_block('block-with-certificates.json', bl, 1)
        dump_json_block('block-with-certificates-expanded.json', bl, 2)

        # advance one epochs for SC1 and let the others cease
        mark_logs("\nLet 1 epoch pass by and send a cert for SC1 only...".  format(EPOCH_LENGTH), self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
            scid1, "sc1", constant1, EPOCH_LENGTH,
            10, CERT_FEE, FT_SC_FEE, MBTR_SC_FEE,
            vCfe, vCmt, [fe1, fe2, fe3, fe4]
        )

        mark_logs("\n==> certificate from SC1 for epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        dump_json_getscinfo('getscinfo-output.json', nodeid=0)

        mark_logs("Let also SC1 cease... ".format(scid2), self.nodes, DEBUG_MODE)

        nbl = int(EPOCH_LENGTH * 1.5)
        mark_logs("Node0 generates {} blocks".format(nbl), self.nodes, DEBUG_MODE)
        # let all sidechains cease
        self.nodes[0].generate(3*EPOCH_LENGTH)
        self.sync_all()

        mark_logs("\nCreate a CSW for SC1 withdrawing coins for two different addresses... ", self.nodes, DEBUG_MODE)

        # CSW sender MC address
        csw_mc_address = self.nodes[0].getnewaddress()

        sc_csw_amount_0 = Decimal('2.0')
        sc_csw_amount_1 = Decimal('1.0')
        null0 = generate_random_field_element_hex()
        null1 = generate_random_field_element_hex()
        actCertData = self.nodes[0].getactivecertdatahash(scid1)['certDataHash']

        ceasingCumScTxCommTree = self.nodes[0].getceasingcumsccommtreehash(scid1)['ceasingCumScTxCommTree']

        sc_proof0 = cswMcTest.create_test_proof(
            "sc1", sc_csw_amount_0, scid1_swapped, null0, csw_mc_address, ceasingCumScTxCommTree,
            cert_data_hash = actCertData, constant = constant1) 

        sc_proof1 = cswMcTest.create_test_proof(
            "sc1", sc_csw_amount_1, scid1_swapped, null1, csw_mc_address, ceasingCumScTxCommTree,
            cert_data_hash = actCertData, constant = constant1) 

        sc_csws = [
            {
                "amount": sc_csw_amount_0,
                "senderAddress": csw_mc_address,
                "scId": scid1,
                "epoch": 0,
                "nullifier": null0,
                "activeCertData": actCertData,
                "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
                "scProof": sc_proof0
            },
            {
                "amount": sc_csw_amount_1,
                "senderAddress": csw_mc_address,
                "scId": scid1,
                "epoch": 0,
                "nullifier": null1,
                "activeCertData": actCertData,
                "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
                "scProof": sc_proof1
            }
        ]

        # recipient MC address
        taddr_0 = self.nodes[0].getnewaddress()
        taddr_1 = self.nodes[1].getnewaddress()
        sc_csw_tx_outs = {
            taddr_0: sc_csw_amount_0,
            taddr_1: sc_csw_amount_1
        }

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sigRawtx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        tx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        mark_logs("sent csw retrieving coins on Node0 and Node1 behalf", self.nodes, DEBUG_MODE)
        self.sync_all()

        print("tx = {}".format(tx))
        dump_json_tx('ceased-sidechain-withdrawal.json', tx)


if __name__ == '__main__':
    scRpcCmdsJsonOutput().main()
