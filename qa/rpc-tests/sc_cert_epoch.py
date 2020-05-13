#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import initialize_chain_clean, assert_equal, \
    start_nodes, stop_nodes, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, mark_logs
import os
from decimal import Decimal
import time

DEBUG_MODE = 1
NUMB_OF_NODES = 4
EPOCH_LENGTH = 5
CERT_FEE = 0.0001


class sc_cert_epoch(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-logtimemicros=1', '-zapwallettxes=2']] * NUMB_OF_NODES )

        for idx, _ in enumerate(self.nodes):
            if idx < (NUMB_OF_NODES-1):
                connect_nodes_bi(self.nodes, idx, idx+1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        # side chain id
        scid = "1111111111111111111111111111111111111111111111111111111111111111"

        # cross-chain transfer amount
        creation_amount = Decimal("0.5")
        fwt_amount = Decimal("30")
        fwt_amount_immature_at_epoch = Decimal("20")
        bwt_amount = Decimal("25")

        blocks = [self.nodes[0].getblockhash(0)]

        mark_logs("Node 1 generates 1 block to prepare coins to spend", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        mark_logs("Node 0 generates 220 block to reach sidechain height", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(220))
        self.sync_all()

        bal_before_sc_creation = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance before SC creation: {}".format(bal_before_sc_creation), self.nodes, DEBUG_MODE)

        sc_creation_tx = self.nodes[1].sc_create(scid, EPOCH_LENGTH, "dada", creation_amount, "abcdef010101abcdef");
        mark_logs("Node 1 created the SC spending {} coins via tx {}.".format(creation_amount, sc_creation_tx), self.nodes, DEBUG_MODE)
        self.sync_all()

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        mark_logs("Node 0 performs a fwd transfer of {} coins to Sc".format(fwt_amount), self.nodes, DEBUG_MODE)
        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount, scid)
        assert(len(fwd_tx) > 0)
        self.sync_all()

        mark_logs("Node0 generates {} blocks, confirming fwd transfer".format(EPOCH_LENGTH - 2), self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(EPOCH_LENGTH - 2))
        self.sync_all()

        mark_logs("Node 0 performs a fwd transfer of {} coins to Sc".format(fwt_amount_immature_at_epoch), self.nodes, DEBUG_MODE)
        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount_immature_at_epoch, scid)
        assert(len(fwd_tx) > 0)
        self.sync_all()

        mark_logs("Node0 generates 1 block moving to epoch 1 but not maturing last fw transfer", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        assert_equal(self.nodes[0].getscinfo(scid)['balance'], creation_amount + fwt_amount)
        assert_equal(self.nodes[0].getscinfo(scid)['immature amounts'][0]['amount'], fwt_amount_immature_at_epoch)

        epoch_number = 0
        epoch_hash = blocks[-1]

        pkh_node2 = self.nodes[2].getnewaddress("", True)
        amounts_bad = [{"pubkeyhash": pkh_node2, "amount": bwt_amount + fwt_amount_immature_at_epoch}]
        amounts = [{"pubkeyhash": pkh_node2, "amount": bwt_amount}]

        mark_logs("Node0 generating 1 block, to show cert for epoch 0 can be received within safeguard blocks", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        node1_balance_ante_cert = self.nodes[1].getbalance()
        node2_balance_ante_cert = self.nodes[2].getbalance()
        node3_balance_ante_cert = self.nodes[3].getbalance()
        mark_logs("Node 1 balance before certificate {}".format(node1_balance_ante_cert), self.nodes, DEBUG_MODE)
        mark_logs("Node 2 balance before certificate {}".format(node2_balance_ante_cert), self.nodes, DEBUG_MODE)
        mark_logs("Node 3 balance before certificate {}".format(node3_balance_ante_cert), self.nodes, DEBUG_MODE)

        mark_logs("Node 0 try to perform a bwd transfer of {} coins to Node2 pkh".format(bwt_amount + fwt_amount_immature_at_epoch, pkh_node2), self.nodes, DEBUG_MODE)
        try:
            self.nodes[0].send_certificate(scid, epoch_number, epoch_hash, amounts_bad, CERT_FEE)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert_equal("sidechain has insufficient funds" in errorString, True)

        mark_logs("Node 0 performs a bwd transfer of {} coins to Node2 pkh".format(bwt_amount, pkh_node2), self.nodes, DEBUG_MODE)
        try:
            cert_epoch_0 = self.nodes[0].send_certificate(scid, epoch_number, epoch_hash, amounts, CERT_FEE)
            assert(len(cert_epoch_0) > 0)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Node0 confims bwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        mark_logs("Checking Nodes wallet's balances are duly updated", self.nodes, DEBUG_MODE)
        assert_equal(node1_balance_ante_cert, self.nodes[1].getbalance())
        assert_equal(node2_balance_ante_cert, self.nodes[2].getbalance())  # Certificate amount is not mature yet
        assert_equal(node3_balance_ante_cert, self.nodes[3].getbalance())

        mark_logs("Checking Sc balance is duly decreased", self.nodes, DEBUG_MODE)
        sc_post_bwd = self.nodes[0].getscinfo(scid)
        assert_equal(sc_post_bwd["balance"], creation_amount + fwt_amount - bwt_amount)

        mark_logs("Checking that Node2 cannot immediately spend coins received from bwd transfer", self.nodes, DEBUG_MODE)
        mark_logs("Node 2 tries to send {} coins to Node3".format(bwt_amount/2), self.nodes, DEBUG_MODE)

        try:
            self.nodes[2].sendtoaddress(self.nodes[3].getnewaddress(), bwt_amount/2)
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert_equal("Insufficient funds" in errorString, True)

        mark_logs("Node0 generates {} blocks, coming to the end of epoch 1".format(EPOCH_LENGTH - 2), self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(EPOCH_LENGTH - 2))
        self.sync_all()

        mark_logs("Node 0 send a certificate with no bwd transfers", self.nodes, DEBUG_MODE)
        try:
            epoch_number = 1
            epoch_hash = blocks[-1]
            cert_epoch_1 = self.nodes[0].send_certificate(scid, epoch_number, epoch_hash, [], CERT_FEE)
            assert(len(cert_epoch_1) > 0)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Node0 confims bwd transfer and moves at safeguard", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(2))
        self.sync_all()

        mark_logs("Checking Nodes wallet's balances are duly updated", self.nodes, DEBUG_MODE)
        assert_equal(node1_balance_ante_cert, self.nodes[1].getbalance())
        assert_equal(node2_balance_ante_cert + bwt_amount, self.nodes[2].getbalance())  # Certificate amount has finally matured
        assert_equal(node3_balance_ante_cert, self.nodes[3].getbalance())

        try:
            speding_bwd_tx = self.nodes[2].sendtoaddress(self.nodes[3].getnewaddress(), bwt_amount/2)
            assert(len(speding_bwd_tx) > 0)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs("Spending bwt founds failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()
        mark_logs("Node0 confims spending of bwd transfer founds generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        mark_logs("Checking Nodes wallet's balances are duly updated", self.nodes, DEBUG_MODE)
        assert_equal(node1_balance_ante_cert, self.nodes[1].getbalance())
        assert_equal(node2_balance_ante_cert + bwt_amount/2 + self.nodes[2].gettransaction(speding_bwd_tx)['fee'], self.nodes[2].getbalance())  # Certificate amount has finally matured
        assert_equal(node3_balance_ante_cert + bwt_amount/2, self.nodes[3].getbalance())

        ### INVALIDATION PHASE 
        mark_logs("Node0 invalidates latest block which confirmed epoch 0 bwd expenditure", self.nodes, DEBUG_MODE)
        block_to_invalidate = self.nodes[0].getbestblockhash()
        self.nodes[0].invalidateblock(block_to_invalidate)
        time.sleep(1)  # Is there a better wait to settle?

        mark_logs("Checking tx speding bwd returns to mempool", self.nodes, DEBUG_MODE)
        assert(speding_bwd_tx in self.nodes[0].getrawmempool())

        mark_logs("Node0 invalidates enough blocks unconfirm epoch 1 certificate", self.nodes, DEBUG_MODE)
        for num in range(1,3):
            block_to_invalidate = self.nodes[0].getbestblockhash()
            self.nodes[0].invalidateblock(block_to_invalidate)
            time.sleep(1)  # Is there a better wait to settle?

        mark_logs("Checking bwd returns to mempool while tx spending epoch 0 certificate gets removed", self.nodes, DEBUG_MODE)
        assert(cert_epoch_1 in self.nodes[0].getrawmempool())
        assert(speding_bwd_tx not in self.nodes[0].getrawmempool()) # speding_bwd_tx would spend an immature cert now since epoch 1 cert is not confirmed anymore

        mark_logs("Checking Sc balance is duly update due to bwd removal", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[0].getscinfo(scid)["balance"], creation_amount + fwt_amount + fwt_amount_immature_at_epoch - bwt_amount)

        mark_logs("Node0 invalidates latest block which signaled end of epoch 1", self.nodes, DEBUG_MODE)
        block_to_invalidate = self.nodes[0].getbestblockhash()
        self.nodes[0].invalidateblock(block_to_invalidate)
        time.sleep(1)  # Is there a better wait to settle?

        mark_logs("Checking both bwd is still mempool", self.nodes, DEBUG_MODE)
        assert(cert_epoch_1 not in self.nodes[0].getrawmempool())
        assert(speding_bwd_tx not in self.nodes[0].getrawmempool())

        mark_logs("Node0 generating 4 block to show bwd has disappeared from history", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(4))
        sc_post_regeneration = self.nodes[0].getscinfo(scid)
        assert_equal(sc_post_regeneration["last certificate epoch"], Decimal(0))
        assert_equal(sc_post_regeneration["balance"], creation_amount + fwt_amount + fwt_amount_immature_at_epoch - bwt_amount)

        mark_logs("Node0 generating 6 block to have longest chain and cause reorg on other nodes", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(6))
        self.sync_all()

        mark_logs("Checking that upon reorg, bwd is erased from other nodes too", self.nodes, DEBUG_MODE)

        for idx, node in enumerate(self.nodes):
            mark_logs("Checking Node{} ScInfos".format(idx), self.nodes, DEBUG_MODE)
            sc_post_regeneration = node.getscinfo(scid)
            assert_equal(sc_post_regeneration["last certificate epoch"], Decimal(0))
            assert_equal(sc_post_regeneration["balance"], creation_amount + fwt_amount + fwt_amount_immature_at_epoch - bwt_amount)
            assert(cert_epoch_1 not in self.nodes[0].getrawmempool())
            assert(speding_bwd_tx not in self.nodes[0].getrawmempool())
            mark_logs("Checking Node{} wallet's balances is duly updated".format(idx), self.nodes, DEBUG_MODE)
            assert_equal(node1_balance_ante_cert, self.nodes[1].getbalance())
            
            # Until ceased sc are handled, coins from cert will mature passed next epoch safeguard
            assert_equal(node2_balance_ante_cert, self.nodes[2].getbalance())
            assert_equal(node3_balance_ante_cert, self.nodes[3].getbalance())

        mark_logs("Checking certificates persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        for idx, node in enumerate(self.nodes):
            mark_logs("Checking Node{} after restart".format(idx), self.nodes, DEBUG_MODE)
            sc_post_regeneration = node.getscinfo(scid)
            assert_equal(sc_post_regeneration["last certificate epoch"], Decimal(0))
            assert_equal(sc_post_regeneration["balance"], creation_amount + fwt_amount + fwt_amount_immature_at_epoch - bwt_amount)
            assert(cert_epoch_1 not in self.nodes[0].getrawmempool())
            assert(speding_bwd_tx not in self.nodes[0].getrawmempool())


if __name__ == '__main__':
    sc_cert_epoch().main()
