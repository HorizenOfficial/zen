#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, stop_nodes, get_epoch_data, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, mark_logs, \
    assert_false, assert_true, swap_bytes
from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.mc_test.mc_test import *
import os
import pprint
import time
from decimal import Decimal
from collections import namedtuple

DEBUG_MODE = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH = 5
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')


class sc_cert_ceasing(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=1','-scproofqueuesize=0', '-logtimemicros=1', '-rescan', '-zapwallettxes=2']] * NUMB_OF_NODES)

        for k in range(0, NUMB_OF_NODES-1):
            connect_nodes_bi(self.nodes, k, k+1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        ''' 
        (1) Create 3 parallel SCs with same epoch length
        (2) Advance epoch
        (3) Send a cert with bwt to sc 1
        (4) Send an empty cert to sc 2
        (5) (do not send any cert to sc 3 and 4)
        (6) Advance epoch
        (7) Reach safe guard --> sc 1,2,4 are alive, sc 3 is ceased
        (8) Mine 3+2 more blocks (i.e. advance epoch)
        (9) Reach safe guard --> all sc are ceased
        (10) Restart nodes
        '''

        # transfer amounts
        creation_amount = []
        bwt_amount = []

        creation_amount.append(Decimal("10.0"))
        creation_amount.append(Decimal("20.0"))
        creation_amount.append(Decimal("30.0"))
        creation_amount.append(Decimal("40.0"))

        bwt_amount.append(Decimal("5.0"))
        bwt_amount.append(Decimal("0.0"))
        bwt_amount.append(Decimal("0.0"))
        bwt_amount.append(Decimal("0.0"))

        mark_logs("Node 0 generates {} blocks".format(ForkHeights['NON_CEASING_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['NON_CEASING_SC'])
        self.sync_all()
        prev_epoch_hash = self.nodes[0].getbestblockhash()

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)

        constant = generate_random_field_element_hex()

        scids = []
        scids_swapped = []
        # SCs creation
        for i in range(0, 4):
            tag = "sc"+str(i+1)
            vk = mcTest.generate_params(tag)
            cmdInput = {
                "version": 0 if i < 3 else 2,
                "withdrawalEpochLength": EPOCH_LENGTH if i < 3 else 0,
                "toaddress": "dada",
                "amount": creation_amount[i],
                "wCertVk": vk,
                "constant": constant,
                "customData": "abcdef"
            }

            ret = self.nodes[0].sc_create(cmdInput)
            creating_tx = ret['txid']
            mark_logs("Node 0 created SC spending {} coins via tx1 {}.".format(creation_amount[i], creating_tx), self.nodes, DEBUG_MODE)
            self.sync_all()
            scids.append(self.nodes[0].getrawtransaction(creating_tx, 1)['vsc_ccout'][0]['scid'])
            scids_swapped.append(str(swap_bytes(scids[i])))
            mark_logs("==> created SC ids {}".format(scids[-1]), self.nodes, DEBUG_MODE)
            
        mark_logs("Node0 generates 5 more blocks to achieve end of withdrawal epochs", self.nodes, DEBUG_MODE)

        self.nodes[0].generate(5)
        self.sync_all()

        # check epoch data are the same for all the scids
        assert_equal(get_epoch_data(scids[0], self.nodes[0], EPOCH_LENGTH), get_epoch_data(scids[1], self.nodes[0], EPOCH_LENGTH))
        assert_equal(get_epoch_data(scids[0], self.nodes[0], EPOCH_LENGTH), get_epoch_data(scids[2], self.nodes[0], EPOCH_LENGTH))

        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scids[0], self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        last_cert_epochs = []
        last_cert_epochs.append(epoch_number)
        last_cert_epochs.append(epoch_number)
        last_cert_epochs.append(-1)
        last_cert_epochs.append(-1) # no cert for sc v3

        # node0 create a cert_1 for funding node1 
        addr_node1 = self.nodes[1].getnewaddress()
        amounts = [{"address": addr_node1, "amount": bwt_amount[0]}]
        mark_logs("Node 0 sends a cert for scid_1 {} with a bwd transfer of {} coins to Node1 address".format(scids[0], bwt_amount[0], addr_node1), self.nodes, DEBUG_MODE)
        try:
            #Create proof for WCert
            quality = 1
            proof = mcTest.create_test_proof("sc1", scids_swapped[0], epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash,
                constant = constant, pks = [addr_node1], amounts = [bwt_amount[0]])

            cert_1 = self.nodes[0].sc_send_certificate(scids[0], epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("==> certificate is {}".format(cert_1), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        self.sync_all()

        # node0 create an empty cert_2 
        mark_logs("Node 0 sends an empty cert for scid_2 {}".format(scids[1]), self.nodes, DEBUG_MODE)
        try:
            #Create proof for WCert
            quality = 1
            proof = mcTest.create_test_proof("sc2", scids_swapped[1], epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash,
                constant = constant)

            cert_2 = self.nodes[0].sc_send_certificate(scids[1], epoch_number, quality,
                epoch_cum_tree_hash, proof, [], FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("==> certificate is {}".format(cert_2), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        # no certs for scid 3, let it cease without having one 
        self.sync_all()

        mark_logs("Node0 generates 5 more blocks to achieve end of withdrawal epochs", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()

        mark_logs("Verifying scs state for all nodes", self.nodes, DEBUG_MODE)
        for k in range(len(scids)):
            for idx, node in enumerate(self.nodes):
                print("idx = {}, k = {}".format(idx, k))
                sc_info = node.getscinfo(scids[k])['items'][0]
                if (k == 2):
                    assert_equal("CEASED", sc_info["state"])
                else:
                    assert_equal("ALIVE", sc_info["state"])
                assert_equal(last_cert_epochs[k], sc_info["lastCertificateEpoch"])
                assert_equal(creation_amount[k] - bwt_amount[k], sc_info["balance"])

        bal1 = self.nodes[1].getbalance()
        print("Balance Node1 = {}\n".format(bal1))
        #assert_equal(bal1, bwt_amount[0])

        mark_logs("Node0 generates 2 more blocks to achieve certs maturity and scs ceasing", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(2)
        self.sync_all()

        bal1 = self.nodes[1].getbalance()
        print("Balance Node1 = {}\n".format(bal1))
        #assert_equal(bal1, bwt_amount[0])

        mark_logs("Node0 generates 3 block to restore Node1 balance ", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(3)
        self.sync_all()

        bal1 = self.nodes[1].getbalance()
        print("Balance Node1 = {}\n".format(bal1))
        #------> TODO fix this in walletassert_equal(bal1, Decimal("0.0"))

        mark_logs("Node 1 tries to send coins to node0...", self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), 1.0)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)


        mark_logs("Verifying scs state for all nodes", self.nodes, DEBUG_MODE)
        for k in range(len(scids)):
            for idx, node in enumerate(self.nodes):
                print("idx = {}, k = {}".format(idx, k))
                sc_info = node.getscinfo(scids[k])['items'][0]
                if (k == 3): # this is a v2 sc
                    assert_equal("ALIVE", sc_info["state"])
                else:
                    assert_equal("CEASED", sc_info["state"])
                assert_equal(last_cert_epochs[k], sc_info["lastCertificateEpoch"])
                assert_equal(creation_amount[k] - bwt_amount[k], sc_info["balance"])

        mark_logs("Node 0 tries to fwd coins to ceased sc {}...".format(scids[2]), self.nodes, DEBUG_MODE)
        fwt_amount = Decimal("0.5")
        mc_return_address = self.nodes[0].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount, "scid": scids[2], 'mcReturnAddress': mc_return_address}]
        try:
            fwd_tx = self.nodes[0].sc_send(cmdInput)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        mark_logs("Node 0 tries to fwd coins to non ceasing sc {}...".format(scids[3]), self.nodes, DEBUG_MODE)
        fwt_amount = Decimal("0.5")
        mc_return_address = self.nodes[0].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount, "scid": scids[3], 'mcReturnAddress': mc_return_address}]
        try:
            fwd_tx = self.nodes[0].sc_send(cmdInput)
        except JSONRPCException as e:
            assert(False)
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)

        self.sync_all()
        # For restarting the nodes we need the startup parameter "-zapwallettxes=2" to remove the forward transfer tx
        # from the wallet, otherwise, after the restart, it would be added to mempool of node 0 but not relayed to node 1
        # causing the wait_bitcoinds() function to hang.

        mark_logs("Checking certificates persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        for k in range(len(scids)):
            for idx, node in enumerate(self.nodes):
                mark_logs("Checking Node{} after restart".format(idx), self.nodes, DEBUG_MODE)
                sc_post_regeneration = node.getscinfo(scids[k])['items'][0]
                if (k == 3): # this is a v2 sc
                    assert_equal("ALIVE", sc_post_regeneration["state"])
                else:
                    assert_equal("CEASED", sc_post_regeneration["state"])
                assert_equal(last_cert_epochs[k], sc_post_regeneration["lastCertificateEpoch"])
                assert_equal(creation_amount[k] - bwt_amount[k], sc_post_regeneration["balance"])

        bal1 = self.nodes[1].getbalance()
        print("Balance Node1 = {}\n".format(bal1))
        #------> TODO fix this in walletassert_equal(bal1, Decimal("0.0"))

        mark_logs("Node0 generates 3 block to restore Node1 balance ", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(3)
        self.sync_all()

        mark_logs("Node 1 tries to send coins to node0...", self.nodes, DEBUG_MODE)
        try:
            tx = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), 1.0)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString, self.nodes, DEBUG_MODE)


if __name__ == '__main__':
    sc_cert_ceasing().main()
