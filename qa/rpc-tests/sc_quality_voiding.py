#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, stop_nodes, mark_logs,\
    get_epoch_data, wait_bitcoinds, assert_false, assert_true, swap_bytes
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH = 20
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')

class quality_voiding(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-scproofqueuesize=0', '-logtimemicros=1', '-rescan']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        The test creates a sc, send funds to it and then sends a certificates to it,
        verifying that backward transfer from lower quality certificates are voided,
        all backward transfer from previously highest quality certificate are voided.
        '''

        # forward transfer amounts
        creation_amount = Decimal("0.5")
        fwt_amount = Decimal("200")
        bwt_amount = Decimal("5")
        bwt_amount_2 = Decimal("20")
        bwt_amount_3 = Decimal("60")

        self.nodes[0].getblockhash(0)

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()

        # SC creation
        bal_before_sc_creation = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance before SC creation: {}".format(bal_before_sc_creation), self.nodes, DEBUG_MODE)

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk_tag = "sc1"
        vk = mcTest.generate_params(vk_tag)
        constant = generate_random_field_element_hex()
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": constant,
        }

        ret = self.nodes[1].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs("Node 1 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        sc_creating_height = self.nodes[0].getblockcount()
        self.sync_all()

        # Check node 1 balance following sc creation
        fee_sc_creation = self.nodes[1].gettransaction(creating_tx)['fee']
        mark_logs("Fee paid for SC creation: {}".format(fee_sc_creation), self.nodes, DEBUG_MODE)
        bal_after_sc_creation = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance after SC creation: {}".format(bal_after_sc_creation), self.nodes, DEBUG_MODE)
        assert_equal(bal_before_sc_creation, bal_after_sc_creation + creation_amount - fee_sc_creation)

        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], Decimal(0))
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts'][0]['amount'], creation_amount)

        # Fwd Transfer to Sc
        bal_before_fwd_tx = self.nodes[0].getbalance("", 0)
        mc_return_address = self.nodes[0].getnewaddress()
        mark_logs("Node0 balance before fwd tx: {}".format(bal_before_fwd_tx), self.nodes, DEBUG_MODE)
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount, "scid": scid, "mcReturnAddress": mc_return_address}]
        fwd_tx = self.nodes[0].sc_send(cmdInput)
        mark_logs("Node0 transfers {} coins to SC with tx {}...".format(fwt_amount, fwd_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node0 confirms fwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # Check node 0 balance following fwd tx
        fee_fwt = self.nodes[0].gettransaction(fwd_tx)['fee']
        mark_logs("Fee paid for fwd tx: {}".format(fee_fwt), self.nodes, DEBUG_MODE)
        bal_after_fwd_tx = self.nodes[0].getbalance("", 0)
        mark_logs("Node0 balance after fwd: {}".format(bal_after_fwd_tx), self.nodes, DEBUG_MODE)
        assert_equal(bal_before_fwd_tx, bal_after_fwd_tx + fwt_amount - fee_fwt - Decimal(MINER_REWARD_POST_H200))

        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], Decimal(0))
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts'][0]['amount'], creation_amount)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts'][1]['amount'], fwt_amount)

        mark_logs("Node0 generating 3 more blocks to achieve end of withdrawal epoch", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(EPOCH_LENGTH - 2)
        self.sync_all()
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], creation_amount + fwt_amount) # Sc balance has matured
        assert_equal(len(self.nodes[0].getscinfo(scid)['items'][0]['immatureAmounts']), 0)

        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        addr_node1 = self.nodes[1].getnewaddress()

        amount_cert_1 = [{"address": addr_node1, "amount": bwt_amount}]

        mark_logs("Node balance: {}".format(self.nodes[0].getbalance()), self.nodes, DEBUG_MODE)
        mark_logs("Node balance: {}".format(self.nodes[1].getbalance()), self.nodes, DEBUG_MODE)

        # Create Cert1 with quality 80 and place it in mempool
        quality = 80
        mark_logs("Create Cert1 with quality {}, bwt transfer {} and place it in mempool".format(quality, bwt_amount), self.nodes, DEBUG_MODE)
        proof = mcTest.create_test_proof(vk_tag,
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant,
                                         pks      = [addr_node1],
                                         amounts  = [bwt_amount])
        try:
            cert_1_epoch_0 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_1, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert(len(cert_1_epoch_0) > 0)
            mark_logs("Certificate of quality {} is {}".format(quality, cert_1_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("Checking mempools alignment", self.nodes, DEBUG_MODE)
        self.sync_all()
        for i in range(1, NUMB_OF_NODES):
            assert_equal(sorted(self.nodes[0].getrawmempool()), sorted(self.nodes[i].getrawmempool()))

        mark_logs("Check cert is in mempools", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_1_epoch_0 in self.nodes[0].getrawmempool())

        # Create Cert2 with higher quality and place it in mempool
        quality = 90
        addr_node1 = self.nodes[1].getnewaddress()
        amount_cert_2 = [{"address": addr_node1, "amount": bwt_amount_2}]
        mark_logs("Create Cert2 with quality {}, bwt transfer {} and place it in mempool".format(quality, bwt_amount_2), self.nodes, DEBUG_MODE)
        proof = mcTest.create_test_proof(vk_tag,
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant,
                                         pks      = [addr_node1],
                                         amounts  = [bwt_amount_2])
        try:
            cert_2_epoch_0 = self.nodes[1].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_2, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert_equal(True, cert_1_epoch_0 in self.nodes[0].getrawmempool())
            assert(len(cert_2_epoch_0) > 0)
            mark_logs("Certificate of quality {} is {}".format(quality, cert_2_epoch_0), self.nodes, DEBUG_MODE)
            assert_equal(True, cert_2_epoch_0 in self.nodes[1].getrawmempool())
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        self.sync_all()
        assert_equal(True, cert_1_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(True, cert_2_epoch_0 in self.nodes[0].getrawmempool())

        mark_logs("Node0 confims bwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        mined = self.nodes[0].generate(1)[0]
        self.sync_all()

        # Check balance
        bwt_address = self.nodes[1].getrawtransaction(cert_2_epoch_0, 1)['vout'][1]['scriptPubKey']['addresses'][0]
        res = self.nodes[1].listtxesbyaddress(bwt_address)
        assert_equal(res[0]['scid'], scid)
        mark_logs("Height : {}".format(res[0]['vout'][1]['maturityHeight']), self.nodes, DEBUG_MODE)
        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        mark_logs("Immature output : {}".format(ud['bwtImmatureOutput']), self.nodes, DEBUG_MODE)
        assert_equal(bwt_amount_2, ud['bwtImmatureOutput'])

        # Checking all certificate are not in mempool
        mark_logs("Check cert is not in mempool anymore", self.nodes, DEBUG_MODE)
        assert_equal(False, cert_1_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(False, cert_2_epoch_0 in self.nodes[0].getrawmempool())

        # Checking all certificate in chain
        assert_true(cert_1_epoch_0 in self.nodes[0].getblock(mined, True)['cert'])
        assert_true(cert_2_epoch_0 in self.nodes[0].getblock(mined, True)['cert'])

        quality = 110
        amount_cert_3 = [{"address": addr_node1, "amount": bwt_amount_3}]
        mark_logs("Create Cert1 with quality {}, bwt transfer {} and place it in mempool".format(quality, bwt_amount_3), self.nodes, DEBUG_MODE)
        proof = mcTest.create_test_proof(vk_tag,
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant,
                                         pks      = [addr_node1],
                                         amounts  = [bwt_amount_3])
        try:
            cert_3_epoch_0 = self.nodes[1].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_3, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            assert (len(cert_3_epoch_0) > 0)
            mark_logs("Certificate of quality {} is {}".format(quality, cert_3_epoch_0), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        # Checking all certificate in mempool
        self.sync_all()
        assert_equal(True, cert_3_epoch_0 in self.nodes[0].getrawmempool())

        mined = self.nodes[0].generate(1)[0]
        self.sync_all()

        # Check balance
        res = self.nodes[1].listtxesbyaddress(bwt_address)
        assert_equal(res[0]['scid'], scid)
        mark_logs("Height : {}".format(res[0]['vout'][1]['maturityHeight']), self.nodes, DEBUG_MODE)
        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        assert_equal(bwt_amount_3, ud['bwtImmatureOutput'])
        mark_logs("Immature output : {}".format(ud['bwtImmatureOutput']), self.nodes, DEBUG_MODE)

        # Checking all certificate in mempool
        mark_logs("Check certificates is not in mempool anymore", self.nodes, DEBUG_MODE)
        assert_equal(False, cert_1_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(False, cert_2_epoch_0 in self.nodes[0].getrawmempool())
        assert_equal(False, cert_3_epoch_0 in self.nodes[0].getrawmempool())

        height = self.nodes[0].getblockcount()
        mark_logs("Check certificates in chain", self.nodes, DEBUG_MODE)
        assert_true(cert_1_epoch_0 in self.nodes[0].getblock(self.nodes[0].getblockhash(height - 1), True)['cert'])
        assert_true(cert_2_epoch_0 in self.nodes[0].getblock(self.nodes[0].getblockhash(height - 1), True)['cert'])
        assert_true(cert_3_epoch_0 in self.nodes[0].getblock(mined, True)['cert'])

        mark_logs("Checking certificates in mempool after block revertion(via invalidateblock)", self.nodes, DEBUG_MODE)
        block_inv = self.nodes[0].getbestblockhash()
        self.nodes[1].invalidateblock(block_inv)
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        sync_blocks(self.nodes[1:NUMB_OF_NODES])

        # Checking certificate in mempool
        assert_false(cert_1_epoch_0 in self.nodes[1].getrawmempool())
        assert_false(cert_2_epoch_0 in self.nodes[1].getrawmempool())
        assert_true(cert_3_epoch_0 in self.nodes[1].getrawmempool())

        # Checking certificates in chain
        assert_true(cert_1_epoch_0 in self.nodes[1].getblock(self.nodes[1].getbestblockhash(), True)['cert'])
        assert_true(cert_2_epoch_0 in self.nodes[1].getblock(self.nodes[1].getbestblockhash(), True)['cert'])
        assert_false(cert_3_epoch_0 in self.nodes[1].getblock(self.nodes[1].getbestblockhash(), True)['cert'])

        # Check balance
        res = self.nodes[1].listtxesbyaddress(bwt_address)
        assert_equal(res[0]['scid'], scid)
        mark_logs("Height : {}".format(res[1]['vout'][1]['maturityHeight']), self.nodes, DEBUG_MODE)
        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        assert_equal(bwt_amount_2, ud['bwtImmatureOutput'])
        mark_logs("Immature output : {}".format(ud['bwtImmatureOutput']), self.nodes, DEBUG_MODE)

        mark_logs("Node 1 reconsider last invalidated block", self.nodes, DEBUG_MODE)
        self.nodes[1].reconsiderblock(block_inv)
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        sync_blocks(self.nodes[1:NUMB_OF_NODES])

        # Checking all certificate in mempool
        mark_logs("Check certificates is not in mempool anymore", self.nodes, DEBUG_MODE)
        assert_equal(False, cert_1_epoch_0 in self.nodes[1].getrawmempool())
        assert_equal(False, cert_2_epoch_0 in self.nodes[1].getrawmempool())
        assert_equal(False, cert_3_epoch_0 in self.nodes[1].getrawmempool())

        height = self.nodes[0].getblockcount()
        mark_logs("Check certificates in chain", self.nodes, DEBUG_MODE)
        assert_true(cert_1_epoch_0 in self.nodes[1].getblock(self.nodes[1].getblockhash(height - 1), True)['cert'])
        assert_true(cert_2_epoch_0 in self.nodes[1].getblock(self.nodes[1].getblockhash(height - 1), True)['cert'])
        assert_true(cert_3_epoch_0 in self.nodes[1].getblock(self.nodes[1].getbestblockhash(), True)['cert'])

        # Check balance
        res = self.nodes[1].listtxesbyaddress(bwt_address)
        assert_equal(res[0]['scid'], scid)
        mark_logs("Height : {}".format(res[1]['vout'][1]['maturityHeight']), self.nodes, DEBUG_MODE)
        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        assert_equal(bwt_amount_3, ud['bwtImmatureOutput'])
        mark_logs("Immature output : {}".format(ud['bwtImmatureOutput']), self.nodes, DEBUG_MODE)

        utxos = self.nodes[1].listunspent()
        assert_equal(len(utxos), 1)
        assert_equal(utxos[0]['txid'], cert_3_epoch_0)

        mark_logs("Reconnect network and check balance.", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        res = self.nodes[1].listtxesbyaddress(bwt_address)
        assert_equal(res[0]['scid'], scid)
        mark_logs("Height : {}".format(res[1]['vout'][1]['maturityHeight']), self.nodes, DEBUG_MODE)
        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        assert_equal(bwt_amount_3, ud['bwtImmatureOutput'])
        mark_logs("Immature output : {}".format(ud['bwtImmatureOutput']), self.nodes, DEBUG_MODE)

        utxos = self.nodes[1].listunspent()
        assert_equal(len(utxos), 1)
        assert_equal(utxos[0]['txid'], cert_3_epoch_0)

        mark_logs("Check voiding of cert1 and cert2", self.nodes, DEBUG_MODE)
        utx_out = self.nodes[1].gettxout(cert_1_epoch_0, 1)
        assert_equal(None, utx_out)

        utx_out = self.nodes[1].gettxout(cert_2_epoch_0, 1)
        assert_equal(None, utx_out)


if __name__ == '__main__':
    quality_voiding().main()
