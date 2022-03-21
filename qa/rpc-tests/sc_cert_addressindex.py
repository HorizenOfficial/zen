#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_false, initialize_chain_clean, assert_equal, \
    start_nodes, get_epoch_data, assert_true, \
    sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
    swap_bytes, to_satoshis, disconnect_nodes
from test_framework.test_framework import MINIMAL_SC_HEIGHT, MINER_REWARD_POST_H200
from test_framework.mc_test.mc_test import generate_random_field_element_hex, CertTestUtils
import os
from decimal import Decimal
import time
import pprint

DEBUG_MODE = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH = 100
CERT_FEE = Decimal("0.000123")
SC_FEE = Decimal("0.000345")
TX_FEE = Decimal("0.000567")
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
SC_COINS_MAT = 2
MINIMAL_SC_HEIGHT = 420

ORDINARY_OUTPUT = 0
TOP_QUALITY_CERT_BACKWARD_TRANSFER = 1
LOW_QUALITY_CERT_BACKWARD_TRANSFER = 2

class sc_cert_addressindex(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args= [['-blockprioritysize=0',
            '-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', 
            '-scproofqueuesize=0', '-logtimemicros=1', '-txindex', '-addressindex', '-sccoinsmaturity=%d' % SC_COINS_MAT]] * NUMB_OF_NODES )

        for idx, _ in enumerate(self.nodes):
            if idx < (NUMB_OF_NODES-1):
                connect_nodes_bi(self.nodes, idx, idx+1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def split_network(self):
    # Split the network of three nodes into nodes 0 and 1.
        assert not self.is_network_split
        disconnect_nodes(self.nodes[0], 1)
        disconnect_nodes(self.nodes[1], 0)
        self.is_network_split = True

    def join_network(self):
        # Join the (previously split) network pieces together: 0-1
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 0)
        # self.sync_all()
        time.sleep(2)
        self.is_network_split = False    

    def run_test(self):

        #amounts
        creation_amount = Decimal("50")
        bwt_amount = Decimal("5")
        tAddr1 = self.nodes[1].getnewaddress()
        node1Addr = self.nodes[1].validateaddress(tAddr1)['address']

        self.nodes[0].generate(MINIMAL_SC_HEIGHT)
        self.sync_all()    
        
        mark_logs("Node 0 generates {} block".format(MINIMAL_SC_HEIGHT), self.nodes, DEBUG_MODE)

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()

        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': EPOCH_LENGTH,
            'toaddress': "dada",
            'amount': creation_amount,
            'wCertVk': vk,
            'constant': constant
        }

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs("Node 1 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        decoded_tx = self.nodes[0].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        sc_creation_block_hash = self.nodes[0].generate(1)[0]
        sc_creation_block = self.nodes[0].getblock(sc_creation_block_hash)
        self.sync_all()

        #Advance for 1 Epoch
        mark_logs("Advance for 1 Epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(EPOCH_LENGTH)
        self.sync_all()

        #Mine Certificate 1 with quality = 5
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        quality = 5
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount])

        amount_cert_1 = [{"address": node1Addr, "amount": bwt_amount}]

        mark_logs("Mine Certificate 1 with quality = {}...".format(quality), self.nodes, DEBUG_MODE)

        cert1 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.sync_all()
        
        maturityHeight = sc_creation_block["height"]+(EPOCH_LENGTH*2)+EPOCH_LENGTH*0.2 - 1

        ####### Test getaddressmempool ########
        addressmempool = self.nodes[1].getaddressmempool({"addresses":[tAddr1]})
        assert_equal(len(addressmempool),1)
        assert_equal(addressmempool[0]['txid'], cert1)
        assert_equal(addressmempool[0]['satoshis'], float(bwt_amount) * 1e8)
        assert_equal(addressmempool[0]['address'], tAddr1)
        assert_equal(addressmempool[0]['outstatus'], TOP_QUALITY_CERT_BACKWARD_TRANSFER)

        self.nodes[0].generate(1)
        self.sync_all()

        ####### Test getaddressmempool ########
        addressmempool = self.nodes[1].getaddressmempool({"addresses":[tAddr1]})
        assert_equal(addressmempool, [])
        ####### Test getaddresstxids ########
        addresstxids = self.nodes[1].getaddresstxids({"addresses":[tAddr1]})
        assert_equal(len(addresstxids),1)
        assert_equal(addresstxids[0],cert1)
        ####### Test getaddressbalance ########
        addressbalance = self.nodes[1].getaddressbalance({"addresses":[tAddr1]})
        addressbalanceWithImmature = self.nodes[1].getaddressbalance({"addresses":[tAddr1]}, True)
        assert_equal(addressbalance["balance"], 0)
        assert_equal(addressbalance["immature"], to_satoshis(bwt_amount))
        assert_equal(addressbalance["received"], 0)
        assert_equal(addressbalanceWithImmature["balance"], to_satoshis(bwt_amount))
        assert_equal(addressbalanceWithImmature["immature"], to_satoshis(bwt_amount))
        assert_equal(addressbalanceWithImmature["received"], to_satoshis(bwt_amount))
        ####### Test getaddressutxo ########
        addressutxo = self.nodes[1].getaddressutxos({"addresses":[tAddr1]})
        assert_equal(len(addressutxo), 0)
        addressutxoWithImmature = self.nodes[1].getaddressutxos({"addresses":[tAddr1]}, True)
        assert_equal(len(addressutxoWithImmature), 1)
        assert_true(addressutxoWithImmature[0]["backwardTransfer"])
        assert_false(addressutxoWithImmature[0]["mature"])
        assert_equal(addressutxoWithImmature[0]["maturityHeight"], maturityHeight)
        assert_equal(addressutxoWithImmature[0]["satoshis"], to_satoshis(bwt_amount))
        currentHeight = self.nodes[0].getblockcount()
        assert_equal(addressutxoWithImmature[0]["blocksToMaturity"], maturityHeight - currentHeight)

        #Add to mempool Certificate 2 with quality = 7
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        quality = 7  
        bwt_amount2 = Decimal("7")      
        mark_logs("Add to mempool Certificate 2 with quality = {}...".format(quality), self.nodes, DEBUG_MODE)
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount2])

        amount_cert_2 = [{"address": node1Addr, "amount": bwt_amount2}]

        cert2 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_2, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.sync_all()

        ####### Test getaddressmempool ########
        addressmempool = self.nodes[1].getaddressmempool({"addresses":[tAddr1]})
        assert_equal(len(addressmempool), 1)
        assert_equal(addressmempool[0]['txid'], cert2)
        assert_equal(addressmempool[0]['satoshis'], float(bwt_amount2) * 1e8)
        assert_equal(addressmempool[0]['address'], tAddr1)
        assert_equal(addressmempool[0]['outstatus'], TOP_QUALITY_CERT_BACKWARD_TRANSFER)

        quality = 9
        bwt_amount3 = Decimal("9")
        mark_logs("Add to mempool Certificate 3 with quality = {}...".format(quality), self.nodes, DEBUG_MODE)
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount3])

        amount_cert_3 = [{"address": node1Addr, "amount": bwt_amount3}]

        cert3 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_3, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        self.sync_all()

        ####### Test getaddressmempool ########
        addressmempool = self.nodes[1].getaddressmempool({"addresses":[tAddr1]})
        assert_equal(len(addressmempool), 2)
        assert_equal(addressmempool[0]['txid'], cert2)
        assert_equal(addressmempool[0]['satoshis'], float(bwt_amount2) * 1e8)
        assert_equal(addressmempool[0]['address'], tAddr1)
        assert_equal(addressmempool[0]['outstatus'], LOW_QUALITY_CERT_BACKWARD_TRANSFER)

        assert_equal(addressmempool[1]['txid'], cert3)
        assert_equal(addressmempool[1]['satoshis'], float(bwt_amount3) * 1e8)
        assert_equal(addressmempool[1]['address'], tAddr1)
        assert_equal(addressmempool[1]['outstatus'], TOP_QUALITY_CERT_BACKWARD_TRANSFER)

        self.nodes[0].generate(1)
        self.sync_all()

        ####### Test getaddresstxids ########
        addresstxids = self.nodes[1].getaddresstxids({"addresses":[tAddr1]})
        assert_equal(len(addresstxids),3)
        assert_true(cert1 in addresstxids and cert2 in addresstxids and cert3 in addresstxids)
        ####### Test getaddressmempool ########
        addressmempool = self.nodes[1].getaddressmempool({"addresses":[tAddr1]})
        assert_equal(len(addressmempool), 0)
        ####### Test getaddressbalance ########
        addressbalance = self.nodes[1].getaddressbalance({"addresses":[tAddr1]})
        addressbalanceWithImmature = self.nodes[1].getaddressbalance({"addresses":[tAddr1]}, True)
        assert_equal(addressbalance["balance"], 0)
        assert_equal(addressbalance["immature"], to_satoshis(bwt_amount3))
        assert_equal(addressbalance["received"], 0)
        assert_equal(addressbalanceWithImmature["balance"], to_satoshis(bwt_amount3))
        assert_equal(addressbalanceWithImmature["immature"], to_satoshis(bwt_amount3))
        assert_equal(addressbalanceWithImmature["received"], to_satoshis(bwt_amount3))
        ####### Test getaddressutxo ########
        addressutxo = self.nodes[1].getaddressutxos({"addresses":[tAddr1]})
        addressutxoWithImmature = self.nodes[1].getaddressutxos({"addresses":[tAddr1]}, True)
        assert_equal(len(addressutxo), 0)
        assert_equal(len(addressutxoWithImmature), 1)
        assert_true(addressutxoWithImmature[0]["backwardTransfer"])
        assert_false(addressutxoWithImmature[0]["mature"])
        assert_equal(addressutxoWithImmature[0]["maturityHeight"], maturityHeight)
        assert_equal(addressutxoWithImmature[0]["satoshis"], to_satoshis(bwt_amount3))
        currentHeight = self.nodes[0].getblockcount()
        assert_equal(addressutxoWithImmature[0]["blocksToMaturity"], maturityHeight - currentHeight)

        # Split the network: (0) / (1)
        mark_logs("\nSplit network", self.nodes, DEBUG_MODE)
        self.split_network()
        mark_logs("The network is split: 0 / 1", self.nodes, DEBUG_MODE)


        #Mine a block with Certificate 4 with quality = 11 and Certificate 5 with quality = 13
        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        quality = 11
        bwt_amount4 = Decimal("11")
        mark_logs("Create a Certificate 4 with quality = {}...".format(quality), self.nodes, DEBUG_MODE)
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount4])

        amount_cert_4 = [{"address": node1Addr, "amount": bwt_amount4}]

        cert4 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_4, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)

        quality = 13
        bwt_amount5 = Decimal("13")
        mark_logs("Create a Certificat 5 with quality = {}...".format(quality), self.nodes, DEBUG_MODE)
        proof = mcTest.create_test_proof(
            "sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant, [node1Addr], [bwt_amount5])

        amount_cert_5 = [{"address": node1Addr, "amount": bwt_amount5}]

        cert5 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
            epoch_cum_tree_hash, proof, amount_cert_5, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)

        lastBlock = self.nodes[0].generate(1)[0]

        # Checking the network chain tips
        mark_logs("\nChecking network chain tips...", self.nodes, DEBUG_MODE)
        print(self.nodes[0].getblockchaininfo()['blocks'])
        print(self.nodes[1].getblockchaininfo()['blocks'])

        assert_equal(self.nodes[0].getblockchaininfo()['blocks'], 524)
        assert_equal(self.nodes[1].getblockchaininfo()['blocks'], 523)

        mark_logs("\nJoining network", self.nodes, DEBUG_MODE)
        self.join_network()
        mark_logs("\nNetwork joined", self.nodes, DEBUG_MODE)

        ####### Test getaddresstxids ########
        addresstxids = self.nodes[1].getaddresstxids({"addresses":[tAddr1]})
        assert_equal(len(addresstxids),5)
        ####### Test getaddressmempool ########
        addressmempool = self.nodes[1].getaddressmempool({"addresses":[tAddr1]})
        assert_equal(len(addressmempool), 0)
        ####### Test getaddressbalance ########
        addressbalance = self.nodes[1].getaddressbalance({"addresses":[tAddr1]})
        addressbalanceWithImmature = self.nodes[1].getaddressbalance({"addresses":[tAddr1]}, True)  
        assert_equal(addressbalance["balance"], 0)
        assert_equal(addressbalance["immature"], to_satoshis(bwt_amount5))
        assert_equal(addressbalance["received"], 0)
        assert_equal(addressbalanceWithImmature["balance"], to_satoshis(bwt_amount5))
        assert_equal(addressbalanceWithImmature["immature"], to_satoshis(bwt_amount5))
        assert_equal(addressbalanceWithImmature["received"], to_satoshis(bwt_amount5))
        ####### Test getaddressutxo ########
        addressutxo = self.nodes[1].getaddressutxos({"addresses":[tAddr1]})
        addressutxoWithImmature = self.nodes[1].getaddressutxos({"addresses":[tAddr1]}, True)
        assert_equal(len(addressutxo), 0)
        assert_equal(len(addressutxoWithImmature), 1)
        assert_true(addressutxoWithImmature[0]["backwardTransfer"])
        assert_false(addressutxoWithImmature[0]["mature"])
        assert_equal(addressutxoWithImmature[0]["maturityHeight"], maturityHeight)
        assert_equal(addressutxoWithImmature[0]["satoshis"], to_satoshis(bwt_amount5))
        currentHeight = self.nodes[0].getblockcount()
        assert_equal(addressutxoWithImmature[0]["blocksToMaturity"], maturityHeight - currentHeight)

        # Checking the network chain tips
        mark_logs("\nChecking network chain tips...", self.nodes, DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            assert_equal(self.nodes[i].getblockchaininfo()['blocks'],524)

        mark_logs("\nInvalidating the last block and checking RPC call results...", self.nodes, DEBUG_MODE)
        self.nodes[1].invalidateblock(lastBlock)
        ####### Test getaddressmempool ########
        addressmempool = self.nodes[1].getaddressmempool({"addresses":[tAddr1]})    
        assert_equal(len(addressmempool), 2)   

        for i in range (0, len(addressmempool)):
            if (addressmempool[i]['txid'] == cert4):
                assert_equal(addressmempool[i]['txid'], cert4)
                assert_equal(addressmempool[i]['satoshis'], float(bwt_amount4) * 1e8)
                assert_equal(addressmempool[i]['address'], tAddr1)
                assert_equal(addressmempool[i]['outstatus'], LOW_QUALITY_CERT_BACKWARD_TRANSFER)
            else:
                assert_equal(addressmempool[i]['txid'], cert5)
                assert_equal(addressmempool[i]['satoshis'], float(bwt_amount5) * 1e8)
                assert_equal(addressmempool[i]['address'], tAddr1)
                assert_equal(addressmempool[i]['outstatus'], TOP_QUALITY_CERT_BACKWARD_TRANSFER)

        ####### Test getaddressbalance ########
        addressbalance = self.nodes[1].getaddressbalance({"addresses":[tAddr1]})
        addressbalanceWithImmature = self.nodes[1].getaddressbalance({"addresses":[tAddr1]}, True)  
        assert_equal(addressbalance["balance"], 0)
        assert_equal(addressbalance["immature"], to_satoshis(bwt_amount3))
        assert_equal(addressbalance["received"], 0)
        assert_equal(addressbalanceWithImmature["balance"], to_satoshis(bwt_amount3))
        assert_equal(addressbalanceWithImmature["immature"], to_satoshis(bwt_amount3))
        assert_equal(addressbalanceWithImmature["received"], to_satoshis(bwt_amount3))        
        ####### Test getaddressutxo ########
        addressutxo = self.nodes[1].getaddressutxos({"addresses":[tAddr1]})
        addressutxoWithImmature = self.nodes[1].getaddressutxos({"addresses":[tAddr1]}, True)
        assert_equal(len(addressutxo), 0)
        assert_equal(len(addressutxoWithImmature), 1)
        assert_true(addressutxoWithImmature[0]["backwardTransfer"])
        assert_false(addressutxoWithImmature[0]["mature"])
        assert_equal(addressutxoWithImmature[0]["maturityHeight"], maturityHeight)
        assert_equal(addressutxoWithImmature[0]["satoshis"], to_satoshis(bwt_amount3))
        currentHeight = self.nodes[1].getblockcount()
        assert_equal(addressutxoWithImmature[0]["blocksToMaturity"], maturityHeight - currentHeight)
        
        
        # Generate blocks to reach maturity height (and also make sidechain cease)
        mark_logs("\nGenerating blocks to make the sidechain ceased...", self.nodes, DEBUG_MODE)
        lastBlock = self.nodes[1].generate(int(maturityHeight - currentHeight))[-1]
        self.sync_all()

        mark_logs("Checking that all the certificates are considered as not mature...", self.nodes, DEBUG_MODE)
        ####### Test getaddressbalance ########
        addressbalance = self.nodes[1].getaddressbalance({"addresses":[tAddr1]})
        addressbalanceWithImmature = self.nodes[1].getaddressbalance({"addresses":[tAddr1]}, True)
        # All the balances should be 0 since the sidechain is ceased and no BT has matured
        assert_equal(addressbalance["balance"], 0)
        assert_equal(addressbalance["immature"], 0)
        assert_equal(addressbalance["received"], 0)
        assert_equal(addressbalanceWithImmature["balance"], 0)
        assert_equal(addressbalanceWithImmature["immature"], 0)
        assert_equal(addressbalanceWithImmature["received"], 0)
        ####### Test getaddressutxo ########
        addressutxo = self.nodes[1].getaddressutxos({"addresses":[tAddr1]})
        addressutxoWithImmature = self.nodes[1].getaddressutxos({"addresses":[tAddr1]}, True)
        assert_equal(len(addressutxo), 0)
        assert_equal(len(addressutxoWithImmature), 0)

        # Invalidate the last block to recover the sidechain from the "ceased" state and make
        # it "alive" again.
        mark_logs("\nInvalidating the last block to make the sidechain alive again...", self.nodes, DEBUG_MODE)
        self.nodes[1].invalidateblock(lastBlock)

        mark_logs("Checking that the certificates are restored as they were before the block revert...", self.nodes, DEBUG_MODE)
        ####### Test getaddressbalance ########
        addressbalance = self.nodes[1].getaddressbalance({"addresses":[tAddr1]})
        addressbalanceWithImmature = self.nodes[1].getaddressbalance({"addresses":[tAddr1]}, True)  
        assert_equal(addressbalance["balance"], 0)
        assert_equal(addressbalance["immature"], to_satoshis(bwt_amount5))
        assert_equal(addressbalance["received"], 0)
        assert_equal(addressbalanceWithImmature["balance"], to_satoshis(bwt_amount5))
        assert_equal(addressbalanceWithImmature["immature"], to_satoshis(bwt_amount5))
        assert_equal(addressbalanceWithImmature["received"], to_satoshis(bwt_amount5))        
        ####### Test getaddressutxo ########
        addressutxo = self.nodes[1].getaddressutxos({"addresses":[tAddr1]})
        addressutxoWithImmature = self.nodes[1].getaddressutxos({"addresses":[tAddr1]}, True)
        assert_equal(len(addressutxo), 0)
        assert_equal(len(addressutxoWithImmature), 1)
        assert_true(addressutxoWithImmature[0]["backwardTransfer"])
        assert_false(addressutxoWithImmature[0]["mature"])
        assert_equal(addressutxoWithImmature[0]["maturityHeight"], maturityHeight)
        assert_equal(addressutxoWithImmature[0]["satoshis"], to_satoshis(bwt_amount5))
        currentHeight = self.nodes[1].getblockcount()
        assert_equal(addressutxoWithImmature[0]["blocksToMaturity"], maturityHeight - currentHeight)

        ret = self.nodes[1].verifychain(4, 0)
        assert_equal(ret, True)


if __name__ == '__main__':
    sc_cert_addressindex().main()
