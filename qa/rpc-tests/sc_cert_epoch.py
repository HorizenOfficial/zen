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

        # forward transfer amount
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

        bal_before = self.nodes[1].getbalance("", 0)
        mark_logs("Node1 balance before SC creation: {}".format(bal_before), self.nodes, DEBUG_MODE)

        creating_tx = self.nodes[1].sc_create(scid, EPOCH_LENGTH, "dada", creation_amount, "abcdef010101abcdef");
        mark_logs("Node 1 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes, DEBUG_MODE)
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

        mark_logs("Node0 generates 1 block maturing first epoch but not maturing last fw transfer", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        epoch_number = 0
        epoch_hash = blocks[-1]

        pkh_node2 = self.nodes[2].getnewaddress("", True)
        amounts_bad = [{"pubkeyhash": pkh_node2, "amount": bwt_amount + fwt_amount_immature_at_epoch}]
        amounts = [{"pubkeyhash": pkh_node2, "amount": bwt_amount}]

        mark_logs("Node0 generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        node1_initial_balance = self.nodes[1].getbalance()
        node2_initial_balance = self.nodes[2].getbalance()
        node3_initial_balance = self.nodes[3].getbalance()
        mark_logs("Node 1 initial balance {}".format(node1_initial_balance), self.nodes, DEBUG_MODE)
        mark_logs("Node 2 initial balance {}".format(node2_initial_balance), self.nodes, DEBUG_MODE)
        mark_logs("Node 3 initial balance {}".format(node3_initial_balance), self.nodes, DEBUG_MODE)

        mark_logs("Node 0 try to prform a bwd transfer of {} coins to Node2 pkh".format(bwt_amount + fwt_amount_immature_at_epoch, pkh_node2), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[0].send_certificate(scid, epoch_number, epoch_hash, amounts_bad, CERT_FEE)
            # expect it to fail because last fwt is not mature
            assert(False)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        assert_equal("sidechain has insufficient funds" in errorString, True)

        mark_logs("Node 0 performs a bwd transfer of {} coins to Node2 pkh".format(bwt_amount, pkh_node2), self.nodes, DEBUG_MODE)
        try:
            cert = self.nodes[0].send_certificate(scid, epoch_number, epoch_hash, amounts, CERT_FEE)
            assert(len(cert) > 0)
        except JSONRPCException, e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        mark_logs("Node0 confims bwd transfer generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        mark_logs("Checking Nodes wallet's balances are duly updated", self.nodes, DEBUG_MODE)
        assert(node1_initial_balance == self.nodes[1].getbalance())
        assert(node2_initial_balance + bwt_amount == self.nodes[2].getbalance()) 
        assert(node3_initial_balance == self.nodes[3].getbalance())

        mark_logs("Checking Sc balance is duly decreased", self.nodes, DEBUG_MODE)
        sc_post_bwd = self.nodes[0].getscinfo(scid)
        assert(sc_post_bwd["balance"] == creation_amount + fwt_amount - bwt_amount)

        mark_logs("Checking that Node2 can spend coins received from bwd transfer", self.nodes, DEBUG_MODE)
        mark_logs("Node 2 sends {}  coins to Node3".format(bwt_amount/2), self.nodes, DEBUG_MODE)
        speding_bwd_tx = self.nodes[2].sendtoaddress(self.nodes[3].getnewaddress(), bwt_amount/2)
        assert(len(speding_bwd_tx) > 0)
        self.sync_all()

        mark_logs("Node0 confims spending of bwd transfer founds generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        mark_logs("Node0 invalidates latest block which confirmed bwd expenditure", self.nodes, DEBUG_MODE)
        block_to_invalidate = self.nodes[0].getbestblockhash()
        self.nodes[0].invalidateblock(block_to_invalidate)
        time.sleep(1)  # Is there a better wait to settle?

        mark_logs("Checking tx speding bwd returns to mempool", self.nodes, DEBUG_MODE)
        assert(speding_bwd_tx in self.nodes[0].getrawmempool())

        mark_logs("Node0 invalidates latest block which confirmed bwd", self.nodes, DEBUG_MODE)
        #raw_input("_______________________")
        block_to_invalidate = self.nodes[0].getbestblockhash()
        self.nodes[0].invalidateblock(block_to_invalidate)
        time.sleep(1)  # Is there a better wait to settle?

        mark_logs("Checking  bwd returns to mempool with tx spending it", self.nodes, DEBUG_MODE)
        assert(cert in self.nodes[0].getrawmempool())
        assert(speding_bwd_tx in self.nodes[0].getrawmempool())

        mark_logs("Checking  Sc balance is duly update due to bwd removal", self.nodes, DEBUG_MODE)
        assert(self.nodes[0].getscinfo(scid)["balance"] == creation_amount + fwt_amount)
        # NOTE: CANNOT CHECK OTHER NODES BALANCES, SINCE I AM WORKING ON A SINGLE BRANCH

        mark_logs("Node0 invalidates latest block which signaled end of epoch", self.nodes, DEBUG_MODE)
        block_to_invalidate = self.nodes[0].getbestblockhash()
        self.nodes[0].invalidateblock(block_to_invalidate)
        time.sleep(1)  # Is there a better wait to settle?

        mark_logs("Checking both bwd and dependant tx is still mempool", self.nodes, DEBUG_MODE)
        assert(cert in self.nodes[0].getrawmempool())
        assert(speding_bwd_tx in self.nodes[0].getrawmempool())

        mark_logs("Node0 invalidates latest block going stricly into epoch 0", self.nodes, DEBUG_MODE)
        block_to_invalidate = self.nodes[0].getbestblockhash()
        self.nodes[0].invalidateblock(block_to_invalidate)
        time.sleep(1)  # Is there a better wait to settle?

        mark_logs("Checking both bwd and dependant tx is are cleared from mempool", self.nodes, DEBUG_MODE)
        assert(cert not in self.nodes[0].getrawmempool())
        assert(speding_bwd_tx not in self.nodes[0].getrawmempool())

        mark_logs("Node0 generating 4 block to show bwd has disappeared from history", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(4))
        sc_post_regeneration = self.nodes[0].getscinfo(scid)
        assert(sc_post_regeneration["last certificate epoch"] == Decimal(-1))
        assert(sc_post_regeneration["balance"] == creation_amount + fwt_amount + fwt_amount_immature_at_epoch)

        mark_logs("Node0 generating 3 block to have longest chain and cause reorg on other nodes", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(3))
        self.sync_all()

        mark_logs("Checking that upon reorg, bwd is erased from other nodes too", self.nodes, DEBUG_MODE)

        for idx, node in enumerate(self.nodes):
            mark_logs("Checking Node{} ScInfos".format(idx), self.nodes, DEBUG_MODE)
            sc_post_regeneration = node.getscinfo(scid)
            assert(sc_post_regeneration["last certificate epoch"] == Decimal(-1))
            assert(sc_post_regeneration["balance"] == creation_amount + fwt_amount + fwt_amount_immature_at_epoch)
            assert(cert not in self.nodes[0].getrawmempool())
            assert(speding_bwd_tx not in self.nodes[0].getrawmempool())
            mark_logs("Checking Node{} wallet's balances is back to initial one".format(idx), self.nodes, DEBUG_MODE)
            assert(node1_initial_balance == self.nodes[1].getbalance())
            assert(node2_initial_balance == self.nodes[2].getbalance())
            assert(node3_initial_balance == self.nodes[3].getbalance())

        mark_logs("Checking certificates persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        for idx, node in enumerate(self.nodes):
            mark_logs("Checking Node{} after restart".format(idx), self.nodes, DEBUG_MODE)
            sc_post_regeneration = node.getscinfo(scid)
            assert(sc_post_regeneration["last certificate epoch"] == Decimal(-1))
            assert(sc_post_regeneration["balance"] == creation_amount + fwt_amount + fwt_amount_immature_at_epoch)
            assert(cert not in self.nodes[0].getrawmempool())
            assert(speding_bwd_tx not in self.nodes[0].getrawmempool())


if __name__ == '__main__':
    sc_cert_epoch().main()
