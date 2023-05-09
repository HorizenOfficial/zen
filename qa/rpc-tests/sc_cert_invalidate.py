#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_false, assert_equal, initialize_chain_clean, get_epoch_data, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, dump_ordered_tips, swap_bytes
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal
import pprint

import time

DEBUG_MODE = 1
EPOCH_LENGTH = 5
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = 0.0001


class sc_cert_invalidate(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(3, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-scproofqueuesize=0', '-logtimemicros=1', '-disablesafemode=1']] * 3)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[1:3])
        sync_mempools(self.nodes[1:3])
        self.is_network_split = split
        self.sync_all()

    def refresh_sidechain(self, sc_info, scid, nIdx = 0 ):
        mark_logs("Node{} generating 1 block".format(nIdx), self.nodes, DEBUG_MODE)
        self.nodes[nIdx].generate(1)
        self.sync_all()
        sc_info.append(self.nodes[nIdx].getscinfo(scid)['items'][0])
        mark_logs("  ==> height {}".format(self.nodes[nIdx].getblockcount()), self.nodes, DEBUG_MODE)

    def run_test(self):

        def removekey(d, key='unconf'):
            r = dict(d)
            for k in d:
                if key in k:
                    del r[k]
            return r

        '''
        Node0 creates a SC and sends funds to it, and then sends a cert to it with a bwt to Node1
        Node0 then sends a few tx with fwt to the same SC and then a second cert with a bwt to Node2 
        Node0 than invalidates all the latest blocks one by one checking sc state and checking also
        that a cert remains in mempool until its end epoch is reverted
        Node0 finally generates a sufficient number of blocks for reverting other nodes' chains and
        the test checks their sc state
        '''

        sc_txes = []
        certs = []

        # forward transfer amount
        creation_amount = Decimal("0.5")
        fwt_amount_1 = Decimal("1000.0")
        fwt_amount_2 = Decimal("1.0")
        fwt_amount_3 = Decimal("2.0")
        fwt_amount_4 = Decimal("3.0")

        bwt_amount_1 = Decimal("8.0")
        bwt_amount_2 = Decimal("8.5")

        sc_info = []

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()


        mark_logs("Node0 creates the SC spending {} coins ...".format(creation_amount), self.nodes, DEBUG_MODE)

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": "dada",
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": constant,
        }

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        sc_info.append(removekey(self.nodes[0].getscinfo(scid)['items'][0]))

        decoded_tx = self.nodes[0].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        mark_logs("creating_tx = {}".format(creating_tx), self.nodes, DEBUG_MODE)
        sc_txes.append(creating_tx)
        self.sync_all()

        prev_epoch_block_hash = self.nodes[0].getblockhash(self.nodes[0].getblockcount())

        self.refresh_sidechain(sc_info, scid)

        sc_creating_height = self.nodes[0].getblockcount()

        mark_logs("Node 0 performs a fwd transfer of {} coins to SC...".format(fwt_amount_1), self.nodes, DEBUG_MODE)
        mc_return_address = self.nodes[0].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount_1, "scid": scid, 'mcReturnAddress': mc_return_address}]
        fwd_tx = self.nodes[0].sc_send(cmdInput)
        print("fwd_tx=" + fwd_tx)
        sc_txes.append(fwd_tx)
        self.sync_all()

        self.refresh_sidechain(sc_info, scid)

        addr_node1 = self.nodes[1].getnewaddress()
        addr_node2 = self.nodes[2].getnewaddress()

        mark_logs("...3 more blocks needed for achieving sc coins maturity", self.nodes, DEBUG_MODE)
        self.refresh_sidechain(sc_info, scid)
        self.refresh_sidechain(sc_info, scid)
        self.refresh_sidechain(sc_info, scid)

        mark_logs("##### End epoch block = {}".format(self.nodes[0].getbestblockhash()), self.nodes, DEBUG_MODE)

        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        mark_logs("Node 0 performs a bwd transfer of {} coins to Node1...".format(bwt_amount_1), self.nodes, DEBUG_MODE)
        amounts = []
        amounts.append({"address": addr_node1, "amount": bwt_amount_1})

        #Create proof for WCert
        quality = 0
        proof = mcTest.create_test_proof("sc1",
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant,
                                         pks      = [addr_node1],
                                         amounts  = [bwt_amount_1])

        cert = self.nodes[0].sc_send_certificate(scid, epoch_number, quality, epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        mark_logs("cert = {}".format(cert), self.nodes, DEBUG_MODE)
        certs.append(cert)
        self.sync_all()

        self.refresh_sidechain(sc_info, scid)

        mark_logs("Node 0 performs a fwd transfer of {} coins to SC...".format(fwt_amount_2), self.nodes, DEBUG_MODE)
        mc_return_address = self.nodes[0].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount_2, "scid": scid, 'mcReturnAddress': mc_return_address}]
        fwd_tx = self.nodes[0].sc_send(cmdInput)
        mark_logs("fwd_tx = {}".format(fwd_tx), self.nodes, DEBUG_MODE)
        sc_txes.append(fwd_tx)
        self.sync_all()

        self.refresh_sidechain(sc_info, scid)

        mark_logs("Node 0 performs a fwd transfer of {} coins to SC...".format(fwt_amount_3), self.nodes, DEBUG_MODE)

        mc_return_address = self.nodes[0].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount_3, "scid": scid, 'mcReturnAddress': mc_return_address}]
        fwd_tx = self.nodes[0].sc_send(cmdInput)

        mark_logs("fwd_tx = {}".format(fwd_tx), self.nodes, DEBUG_MODE)
        sc_txes.append(fwd_tx)
        self.sync_all()

        self.refresh_sidechain(sc_info, scid)

        mark_logs("Node 0 performs a fwd transfer of {} coins to SC...".format(fwt_amount_4), self.nodes, DEBUG_MODE)
        mc_return_address = self.nodes[0].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount_4, "scid": scid, 'mcReturnAddress': mc_return_address}]
        fwd_tx = self.nodes[0].sc_send(cmdInput)

        mark_logs("fwd_tx = {}".format(fwd_tx), self.nodes, DEBUG_MODE)
        sc_txes.append(fwd_tx)
        self.sync_all()

        self.refresh_sidechain(sc_info, scid)

        self.refresh_sidechain(sc_info, scid)

        mark_logs("##### End epoch block = {}".format(self.nodes[0].getbestblockhash()), self.nodes, DEBUG_MODE)

        self.refresh_sidechain(sc_info, scid)

        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        sc_creating_height = self.nodes[0].getscinfo(scid)['items'][0]['createdAtBlockHeight']
        epoch_block_hash = self.nodes[0].getblockhash(sc_creating_height - 1 + ((epoch_number + 1) * EPOCH_LENGTH))

        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        mark_logs("Node 0 performs a bwd transfer of {} coins to Node2...".format(bwt_amount_2), self.nodes, DEBUG_MODE)
        amounts = []
        amounts.append({"address": addr_node2, "amount": bwt_amount_2})

        #Create proof for WCert
        quality = 1
        proof = mcTest.create_test_proof("sc1",
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         constant = constant,
                                         pks      = [addr_node2],
                                         amounts  = [bwt_amount_2])

        cert = self.nodes[0].sc_send_certificate(scid, epoch_number, quality, epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
        mark_logs("cert = {}".format(cert), self.nodes, DEBUG_MODE)
        certs.append(cert)
        self.sync_all()
        
        self.refresh_sidechain(sc_info, scid)

        old_bal = self.nodes[0].getbalance()
        
        cross_epoch_1 = False
        cross_epoch_0 = False

        end_epoch_height = self.nodes[0].getblock(epoch_block_hash)['height']

        # invalidate all blocks one by one
        for j in range(0, len(sc_info)):
            inv_hash = self.nodes[0].getbestblockhash()
            inv_heigth = self.nodes[0].getblockcount()
            mark_logs("Node 0 invalidates last block of height = {}".format(inv_heigth), self.nodes, DEBUG_MODE)
            self.nodes[0].invalidateblock(inv_hash)
            sync_mempools(self.nodes[1:3])
            sc_info.pop()

            # check that last cert is always in mempool until end epoch is reverted
            if (end_epoch_height < inv_heigth):
                assert_true(certs[1] in self.nodes[0].getrawmempool())
                mark_logs("cert[{}] is in mempool".format(certs[1]), self.nodes, DEBUG_MODE)
            else:
                assert_false(certs[1] in self.nodes[0].getrawmempool())

            # list are empty, exit loop
            if (len(sc_info) == 0):
                break

            try:
                ret = removekey(self.nodes[0].getscinfo(scid)['items'][0])
                assert_equal(ret, sc_info[-1])
            except JSONRPCException as e:
                errorString = e.error['message']
                print(errorString)
                if (inv_heigth > sc_creating_height):
                    assert(False)
                assert_true(creating_tx in self.nodes[0].getrawmempool())
                mark_logs("creating tx[{}] is in mempool".format(creating_tx), self.nodes, DEBUG_MODE)

        sync_mempools(self.nodes[1:3])

        mempool = self.nodes[0].getrawmempool()

        mark_logs("check that mempool has only sc transactions (no certs)", self.nodes, DEBUG_MODE)
        for k in mempool:
            assert_equal((k in sc_txes), True)

        # if a coinbase used as input for a tx gets immature during invalidation, the tx is removed
        # from mempool and the assert fails.
        # In this case the 1000 coins fwd is sometime removed from the mempool since it spends a coinbase become immature 
        # TODO what will happen to these funds in the real cases?

        # for m in sc_txes:
        #    # check that all sc transactions are in mempool
        #    assert_equal((m in mempool), True)

        sc_amount = Decimal("0.0")
        sc_fee = Decimal("0.0") 
        for m in mempool:
            a = self.nodes[0].gettransaction(m)['amount']
            f = self.nodes[0].gettransaction(m)['fee']
            sc_amount -= a
            sc_fee -= f

        h0 = self.nodes[0].getblockcount()
        h1 = self.nodes[1].getblockcount()
        delta = h1 - (h0)
        numbofblocks = int(delta * (delta + 1) / 2)

        mark_logs("Node0 generating {} more blocks for reverting the other chains".format(numbofblocks), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(numbofblocks)
        self.sync_all()

        activeChain = []
        for i in range(0, 3):
            chaintips = self.nodes[i].getchaintips()
            for k in chaintips:
                if k['status'] == "active":
                    activeChain.append(k)
                    break

        mark_logs("check that active chain is the same for all nodes", self.nodes, DEBUG_MODE)
        assert_equal(activeChain[0], activeChain[1])
        assert_equal(activeChain[0], activeChain[2])

        mark_logs("check that sc is the same for all nodes", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[0].getscinfo(scid), self.nodes[1].getscinfo(scid))
        assert_equal(self.nodes[0].getscinfo(scid), self.nodes[2].getscinfo(scid))

        mark_logs("check that sc balance is the amount we had in mempool at the end of invalidation phase", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[0].getscinfo(scid)['items'][0]['balance'], sc_amount)

        print("Node0 balance before: ", old_bal)
        print("Node0 balance now   : ", self.nodes[0].getbalance())
        print("            sc fee  : ", sc_fee)

if __name__ == '__main__':
    sc_cert_invalidate().main()
