#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Exercise the wallet backup code.  Ported from walletbackup.sh.

Test case is:
4 nodes. 1 2 and 3 send transactions between each other,
fourth node is a miner.
1 2 3 each mine a block to start, then
Miner creates 100 blocks so 1 2 3 each have 40 mature
coins to spend.
Then 5 iterations of 1/2/3 sending coins amongst
themselves to get transactions in the wallets,
and the miner mining one block.

Wallets are backed up using dumpwallet/backupwallet.
Then 5 more iterations of transactions and mining a block.

Miner then generates 101 more blocks, so any
transaction fees paid mature.

Sanity check:
  Sum(1,2,3,4 balances) == 114*40

1/2/3 are shutdown, and their wallets erased.
Then restore using wallet.dat backup. And
confirm 1/2/3/4 balances are same as before.

Shutdown again, restore using importwallet,
and confirm again balances are correct.
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    sync_blocks, sync_mempools, wait_bitcoinds, mark_logs, \
    assert_false, assert_true, swap_bytes, wait_and_assert_operationid_status, \
    start_nodes, start_node, connect_nodes, stop_nodes, stop_node, get_epoch_data

from test_framework.mc_test.mc_test import *

import os
import shutil
from random import randint
from decimal import Decimal
import logging
import pprint

DEBUG_MODE = 1
EPOCH_LENGTH = 16
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')



logging.basicConfig(format='%(levelname)s:%(message)s', level=logging.INFO)

class WalletBackupTest(BitcoinTestFramework):

    def setup_chain(self):
        logging.info("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4)
        self.firstRound = True

    # This mirrors how the network was setup in the bash test
    def setup_network(self, split=False):
        # -exportdir option means we must provide a valid path to the destination folder for wallet backups
        ed0 = "-exportdir=" + self.options.tmpdir + "/node0"
        ed1 = "-exportdir=" + self.options.tmpdir + "/node1"
        ed2 = "-exportdir=" + self.options.tmpdir + "/node2"

        # nodes 1, 2,3 are spenders, let's give them a keypool=100
        extra_args = [
           ['-debug=zrpc', "-keypool=100", ed0],
           ['-debug=zrpc', "-keypool=100", ed1],
           ['-debug=zrpc', "-keypool=100", ed2],
           []]
        self.nodes = start_nodes(4, self.options.tmpdir, extra_args)
        connect_nodes(self.nodes[0], 3)
        connect_nodes(self.nodes[1], 3)
        connect_nodes(self.nodes[2], 3)
        connect_nodes(self.nodes[2], 0)
        self.is_network_split=False
        self.sync_all()

    def one_send(self, from_node, to_address):
        if (randint(1,2) == 1):
            amount = Decimal(randint(1,10)) / Decimal(10)
            self.nodes[from_node].sendtoaddress(to_address, amount)

    def do_one_round(self):
        a0 = self.nodes[0].getnewaddress()
        a1 = self.nodes[1].getnewaddress()
        a2 = self.nodes[2].getnewaddress()

        self.one_send(0, a1)
        self.one_send(0, a2)
        self.one_send(1, a0)
        self.one_send(1, a2)
        self.one_send(2, a0)
        self.one_send(2, a1)

        # Have the miner (node3) mine a block.
        # Must sync mempools before mining.
        sync_mempools(self.nodes)
        self.nodes[3].generate(1)

    # As above, this mirrors the original bash test.
    def start_three(self):
        # -exportdir option means we must provide a valid path to the destination folder for wallet backups
        ed0 = "-exportdir=" + self.options.tmpdir + "/node0"
        ed1 = "-exportdir=" + self.options.tmpdir + "/node1"
        ed2 = "-exportdir=" + self.options.tmpdir + "/node2"

        self.nodes[0] = start_node(0, self.options.tmpdir, ['-debug=zrpc', ed0])
        self.nodes[1] = start_node(1, self.options.tmpdir, ['-debug=zrpc', ed1])
        self.nodes[2] = start_node(2, self.options.tmpdir, ['-debug=zrpc', ed2])

        connect_nodes(self.nodes[0], 3)
        connect_nodes(self.nodes[1], 3)
        connect_nodes(self.nodes[2], 3)
        connect_nodes(self.nodes[2], 0)

    def stop_three(self):
        stop_node(self.nodes[0], 0)
        stop_node(self.nodes[1], 1)
        stop_node(self.nodes[2], 2)

    def erase_three(self):
        os.remove(self.options.tmpdir + "/node0/regtest/wallet.dat")
        os.remove(self.options.tmpdir + "/node1/regtest/wallet.dat")
        os.remove(self.options.tmpdir + "/node2/regtest/wallet.dat")
    
    def erase_dumps(self):
        os.remove(self.options.tmpdir + "/node0/walletdumpcert")
        os.remove(self.options.tmpdir + "/node1/walletdumpcert")
        os.remove(self.options.tmpdir + "/node2/walletdumpcert")
        os.remove(self.options.tmpdir + "/node0/walletbakcert")
        os.remove(self.options.tmpdir + "/node1/walletbakcert")
        os.remove(self.options.tmpdir + "/node2/walletbakcert")

    def run_test_with_scversion(self, scversion, ceasable = True):

            sc_name = "sc" + str(scversion) + str(ceasable)
            epoch_length = 0 if not ceasable else EPOCH_LENGTH
            tmpdir = self.options.tmpdir

            # reach sidechain fork
            if self.firstRound:
                nb = int(self.nodes[0].getblockcount())
                nb_to_gen = ForkHeights['NON_CEASING_SC'] - nb
                if nb_to_gen > 0:
                    mark_logs("Node 0 generates {} block".format(nb_to_gen), self.nodes, DEBUG_MODE)
                    self.nodes[0].generate(nb_to_gen)
                    self.sync_all()

            if ceasable:
                safe_guard_size = epoch_length//5
                if safe_guard_size < 2:
                    safe_guard_size = 2
            else:
                safe_guard_size = 1

            creation_amount = Decimal("1.0")
            bwt_amount1     = Decimal("0.10")
            bwt_amount2     = Decimal("0.20")
            bwt_amount3     = Decimal("0.40")

            #generate wCertVk and constant
            mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
            vk = mcTest.generate_params(sc_name) if scversion < 2 else mcTest.generate_params(sc_name, keyrot = True)
            constant = generate_random_field_element_hex()

            # do a shielded transaction for testing zaddresses in backup
            mark_logs("node0 shields its coinbase", self.nodes, DEBUG_MODE)
            zaddr0 = self.nodes[0].z_getnewaddress()
            res = self.nodes[0].z_shieldcoinbase("*", zaddr0)
            wait_and_assert_operationid_status(self.nodes[0], res['opid'])
            self.sync_all()
            
            self.nodes[0].generate(1)
            self.sync_all()

            # node0 z_sends to node1
            mark_logs("node0 z_send to node1", self.nodes, DEBUG_MODE)
            zaddr1 = self.nodes[1].z_getnewaddress()
            opid = self.nodes[0].z_sendmany(zaddr0, [{"address": zaddr1, "amount": Decimal("1.234")}])
            wait_and_assert_operationid_status(self.nodes[0], opid)
            self.sync_all()

            # Create a SC
            cmdInput = {
                'version': scversion,
                'withdrawalEpochLength': epoch_length,
                'toaddress': "dada",
                'amount': creation_amount,
                'wCertVk': vk,
                'constant': constant
            }

            ret = self.nodes[0].sc_create(cmdInput)
            scid = ret['scid']
            scid_swapped = str(swap_bytes(scid))
            mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)
            self.sync_all()

            mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
            self.nodes[0].generate(1)
            sc_creating_height = self.nodes[0].getblockcount()
            self.sync_all()

            sc_confirm_height = self.nodes[0].getblockcount()

            tempnblocks = epoch_length - 1 if ceasable else 3
            mark_logs("Node0 generates {} more blocks to achieve end of withdrawal epochs".format(tempnblocks), self.nodes, DEBUG_MODE)
            self.nodes[0].generate(tempnblocks)
            self.sync_all()

            current_reference_height1 = self.nodes[0].getblockcount() - 2
            epoch_number, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[0], epoch_length, not ceasable, current_reference_height1)
            if not ceasable:
                epoch_number = 0

            n1_initial_balance = self.nodes[1].getbalance() 

            # node0 create a cert_1 for funding node1 
            # skip default address, just to use a brand new one 
            self.nodes[1].getnewaddress()

            addr_node1 = self.nodes[1].getnewaddress()
            amounts = [{"address": addr_node1, "amount": bwt_amount1}, {"address": addr_node1, "amount": bwt_amount2}]
            mark_logs("Node 0 sends a cert for scid {} with 2 bwd transfers of {} coins to Node1 address".format(scid, bwt_amount1+bwt_amount2, addr_node1), self.nodes, DEBUG_MODE)
            try:
                #Create proof for WCert
                quality = 1

                proof = mcTest.create_test_proof(sc_name,
                                                 scid_swapped,
                                                 epoch_number,
                                                 quality,
                                                 MBTR_SC_FEE,
                                                 FT_SC_FEE,
                                                 epoch_cum_tree_hash,
                                                 prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                                 constant = constant,
                                                 pks = [addr_node1, addr_node1],
                                                 amounts = [bwt_amount1, bwt_amount2])

                cert_1 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                    epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
                mark_logs("==> certificate is {}".format(cert_1), self.nodes, DEBUG_MODE)
                self.sync_all()
            except JSONRPCException as e:
                errorString = e.error['message']
                mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
                assert(False)

            # start first epoch + 2*epocs + safe guard
            bwtMaturityHeight = (sc_creating_height-1) + 2*epoch_length + safe_guard_size

            # get the taddr of Node1 where the bwt is sent to
            bwt_address = self.nodes[0].getrawtransaction(cert_1, 1)['vout'][1]['scriptPubKey']['addresses'][0]

            mark_logs("Node0 generates {} more blocks to achieve end of withdrawal epochs".format(epoch_length), self.nodes, DEBUG_MODE)
            self.nodes[0].generate(epoch_length)
            self.sync_all()

            current_reference_height2 = self.nodes[0].getblockcount() - 1
            epoch_number, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[0], epoch_length, not ceasable, current_reference_height2)

            if not ceasable:
                epoch_number = 1
            mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

            # node0 create a cert_2 for funding node1 
            amounts = [{"address": addr_node1, "amount": bwt_amount3}]
            mark_logs("Node 0 sends a cert for scid {} with 1 bwd transfers of {} coins to Node1 address".format(scid, bwt_amount3, addr_node1), self.nodes, DEBUG_MODE)
            try:
                #Create proof for WCert
                quality = 1
                proof = mcTest.create_test_proof(sc_name,
                                                 scid_swapped,
                                                 epoch_number,
                                                 quality,
                                                 MBTR_SC_FEE,
                                                 FT_SC_FEE,
                                                 epoch_cum_tree_hash,
                                                 prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                                 constant = constant,
                                                 pks = [addr_node1],
                                                 amounts = [bwt_amount3])

                cert_2 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                    epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
                mark_logs("==> certificate is {}".format(cert_2), self.nodes, DEBUG_MODE)
                self.sync_all()
            except JSONRPCException as e:
                errorString = e.error['message']
                mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
                assert(False)
            
            # mature the first bwts but not the latest, so we do not have it in balance 
            mark_logs("Node0 generates {} more block attaining the maturity of the first pair of bwts".format(safe_guard_size), self.nodes, DEBUG_MODE)
            self.nodes[0].generate(safe_guard_size)
            self.sync_all()

            mark_logs("Check Node1 now has bwts in its balance, and their maturity height is as expected", self.nodes, DEBUG_MODE)
            # The following asserts on bwtMaturityHeight are meaningful only for ceasing sidechains. For non ceasing sc,
            # we can check that certs have matured by looking at the balance, as we create only four
            # blocks since sc creation confirmation. Also, bwt mature immediately, hence balance is affected at once.
            if ceasable:
                assert_equal(self.nodes[1].getblockcount(), bwtMaturityHeight)
                assert_equal(self.nodes[1].getbalance(), n1_initial_balance+bwt_amount1 + bwt_amount2) 
                assert_equal(self.nodes[1].z_getbalance(bwt_address), bwt_amount1 + bwt_amount2)
            else:
                assert_equal(self.nodes[1].getblockcount(), sc_confirm_height + 4)
                assert_equal(self.nodes[1].getbalance(), n1_initial_balance + bwt_amount1 + bwt_amount2 + bwt_amount3) 
                assert_equal(self.nodes[1].z_getbalance(bwt_address), bwt_amount1 + bwt_amount2 + bwt_amount3)

            balance0 = self.nodes[0].getbalance()
            balance1 = self.nodes[1].getbalance()
            balance2 = self.nodes[2].getbalance()

            z_balance0 = self.nodes[0].z_gettotalbalance()
            z_balance1 = self.nodes[1].z_gettotalbalance()
            z_balance2 = self.nodes[2].z_gettotalbalance()

            logging.info("Backing up")
            self.nodes[0].backupwallet("walletbakcert")
            self.nodes[0].z_exportwallet("walletdumpcert")

            self.nodes[1].backupwallet("walletbakcert")
            self.nodes[1].z_exportwallet("walletdumpcert")

            # test legacy version of cmd, we do not have z txes
            self.nodes[2].backupwallet("walletbakcert")
            self.nodes[2].dumpwallet("walletdumpcert")

            ##
            # Test restoring spender wallets from backups
            ##
            logging.info("Restoring using wallet.dat")
            self.stop_three()
            self.erase_three()

            # Restore wallets from backup
            shutil.copyfile(tmpdir + "/node0/walletbakcert", tmpdir + "/node0/regtest/wallet.dat")
            shutil.copyfile(tmpdir + "/node1/walletbakcert", tmpdir + "/node1/regtest/wallet.dat")
            shutil.copyfile(tmpdir + "/node2/walletbakcert", tmpdir + "/node2/regtest/wallet.dat")

            logging.info("Re-starting nodes")
            self.start_three()
            sync_blocks(self.nodes)

            assert_equal(self.nodes[0].getbalance(), balance0)
            assert_equal(self.nodes[1].getbalance(), balance1)
            assert_equal(self.nodes[2].getbalance(), balance2)

            logging.info("Restoring using dumped wallet")
            self.stop_three()
            self.erase_three()

            self.start_three()

            assert_equal(self.nodes[0].getbalance(), 0)
            assert_equal(self.nodes[1].getbalance(), 0)
            assert_equal(self.nodes[2].getbalance(), 0)

            self.nodes[0].z_importwallet(tmpdir + "/node0/walletdumpcert")
            self.nodes[1].z_importwallet(tmpdir + "/node1/walletdumpcert")
            self.nodes[2].importwallet(tmpdir + "/node2/walletdumpcert")

            sync_blocks(self.nodes)

            assert_equal(self.nodes[0].getbalance(), balance0)
            assert_equal(self.nodes[1].getbalance(), balance1)
            assert_equal(self.nodes[2].getbalance(), balance2)

            assert_equal(z_balance0, self.nodes[0].z_gettotalbalance())
            assert_equal(z_balance1, self.nodes[1].z_gettotalbalance())
            assert_equal(z_balance2, self.nodes[2].z_gettotalbalance())

            self.erase_dumps()

    def run_test(self):
        logging.info("Generating initial blockchain")
        self.nodes[0].generate(1)
        sync_blocks(self.nodes)
        self.nodes[1].generate(1)
        sync_blocks(self.nodes)
        self.nodes[2].generate(1)
        sync_blocks(self.nodes)
        self.nodes[3].generate(100)
        sync_blocks(self.nodes)

        assert_equal(self.nodes[0].getbalance(), 11.4375)
        assert_equal(self.nodes[1].getbalance(), 11.4375)
        assert_equal(self.nodes[2].getbalance(), 11.4375)
        assert_equal(self.nodes[3].getbalance(), 0)

        logging.info("Creating transactions")
        # Five rounds of sending each other transactions.
        for i in range(5):
            self.do_one_round()

        logging.info("Backing up")
        tmpdir = self.options.tmpdir
        self.nodes[0].backupwallet("walletbak")
        self.nodes[0].dumpwallet("walletdump")
        self.nodes[1].backupwallet("walletbak")
        self.nodes[1].dumpwallet("walletdump")
        self.nodes[2].backupwallet("walletbak")
        self.nodes[2].dumpwallet("walletdump")

        # Verify dumpwallet cannot overwrite an existing file
        try:
            self.nodes[2].dumpwallet("walletdump")
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            assert("Cannot overwrite existing file" in errorString)

        logging.info("More transactions")
        for i in range(5):
            self.do_one_round()

        # Generate 101 more blocks, so any fees paid mature
        self.nodes[3].generate(101)
        self.sync_all()

        balance0 = self.nodes[0].getbalance()
        balance1 = self.nodes[1].getbalance()
        balance2 = self.nodes[2].getbalance()
        balance3 = self.nodes[3].getbalance()
        total = balance0 + balance1 + balance2 + balance3

        # At this point, there are 214 blocks (103 for setup, then 10 rounds, then 101.)
        # 114 are mature, so the sum of all wallets should be 100*11.4375 + 4 * 11 + 10*8.75 = 1275.25
        assert_equal(total, 1275.25)

        ##
        # Test restoring spender wallets from backups
        ##
        logging.info("Restoring using wallet.dat")
        self.stop_three()
        self.erase_three()

        # Start node2 with no chain
        shutil.rmtree(self.options.tmpdir + "/node2/regtest/blocks")
        shutil.rmtree(self.options.tmpdir + "/node2/regtest/chainstate")

        # Restore wallets from backup
        shutil.copyfile(tmpdir + "/node0/walletbak", tmpdir + "/node0/regtest/wallet.dat")
        shutil.copyfile(tmpdir + "/node1/walletbak", tmpdir + "/node1/regtest/wallet.dat")
        shutil.copyfile(tmpdir + "/node2/walletbak", tmpdir + "/node2/regtest/wallet.dat")

        logging.info("Re-starting nodes")
        self.start_three()
        sync_blocks(self.nodes)

        assert_equal(self.nodes[0].getbalance(), balance0)
        assert_equal(self.nodes[1].getbalance(), balance1)
        assert_equal(self.nodes[2].getbalance(), balance2)

        logging.info("Restoring using dumped wallet")
        self.stop_three()
        self.erase_three()

        #start node2 with no chain
        shutil.rmtree(self.options.tmpdir + "/node2/regtest/blocks")
        shutil.rmtree(self.options.tmpdir + "/node2/regtest/chainstate")

        self.start_three()

        assert_equal(self.nodes[0].getbalance(), 0)
        assert_equal(self.nodes[1].getbalance(), 0)
        assert_equal(self.nodes[2].getbalance(), 0)

        self.nodes[0].importwallet(tmpdir + "/node0/walletdump")
        self.nodes[1].importwallet(tmpdir + "/node1/walletdump")
        self.nodes[2].importwallet(tmpdir + "/node2/walletdump")

        sync_blocks(self.nodes)

        assert_equal(self.nodes[0].getbalance(), balance0)
        assert_equal(self.nodes[1].getbalance(), balance1)
        assert_equal(self.nodes[2].getbalance(), balance2)

        ### Wallet tests on sidechains
        mark_logs("\n**SC version 0", self.nodes, DEBUG_MODE)
        self.run_test_with_scversion(0)
        mark_logs("\n**SC version 2 - ceasable SC", self.nodes, DEBUG_MODE)
        self.run_test_with_scversion(2, True)
        mark_logs("\n**SC version 2 - non-ceasable SC", self.nodes, DEBUG_MODE)
        self.run_test_with_scversion(2, False)


if __name__ == '__main__':
    WalletBackupTest().main()
