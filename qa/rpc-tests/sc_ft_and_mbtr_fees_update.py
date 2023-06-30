#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_greater_than, initialize_chain_clean, \
    start_nodes, stop_nodes, wait_bitcoinds, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs,\
    get_epoch_data, swap_bytes, disconnect_nodes ##, colorize as cc
from test_framework.test_framework import ForkHeights
from test_framework.mc_test.mc_test import CertTestUtils, generate_random_field_element_hex
from decimal import Decimal
import math

DEBUG_MODE    = 1
NUMB_OF_NODES = 4
EPOCH_LENGTH  = 10  # In regtest, we have to choose this very short epoch length (see function
                    # CSidechain::getMaxSizeOfScFeesContainers())
FT_SC_FEE     = Decimal('0.01')
MBTR_SC_FEE   = Decimal('0.03')
CERT_FEE      = Decimal('0.015')

NUM_TXS       = 10

#### temporary logs functions
def cc(c, inp):
    return inp
def mark_logs_temp(msg, nodes, debug = 0, color = 'n'):
    return mark_logs(msg, nodes, debug)
####

class provaFee(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
            extra_args=[['-scproofqueuesize=0', '-logtimemicros=1', '-debug=sc', '-debug=py', '-debug=mempool', '-debug=net', '-debug=bench']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 3)
        self.is_network_split = split
        self.sync_all()
        self.firstRound = True

    def assert_txs_in_mempool(self, expected_txs_in_nodes_0_and_1):
        assert_equal(len(self.nodes[0].getrawmempool()), expected_txs_in_nodes_0_and_1)
        assert_equal(len(self.nodes[1].getrawmempool()), expected_txs_in_nodes_0_and_1)
        assert_equal(len(self.nodes[2].getrawmempool()), 0)
        assert_equal(len(self.nodes[3].getrawmempool()), 0)

    def assert_chain_height_difference(self, difference_in_nodes_0_and_1):
        temp_blockCount = self.nodes[2].getblockcount()
        assert_equal(temp_blockCount, self.nodes[0].getblockcount() + difference_in_nodes_0_and_1)
        assert_equal(temp_blockCount, self.nodes[1].getblockcount() + difference_in_nodes_0_and_1)
        assert_equal(temp_blockCount, self.nodes[3].getblockcount())


    #######################
    def run_test_with_scversion(self, scversion, ceasable = True):
        # amounts
        creation_amount = Decimal("100")
        bwt_amount = Decimal("1.0")
        sc_name = "sc" + str(scversion) + str(ceasable)
        address = "dada"

        # Adjust epoch length according to sc specifications
        epoch_length = 0 if not ceasable else EPOCH_LENGTH

        if (self.firstRound):
            mark_logs_temp(f"Node 0 generates {ForkHeights['MINIMAL_SC']} block", self.nodes, DEBUG_MODE, color='e')
            self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
            self.sync_all()
            self.firstRound = False
        else:
            # if sc version 2, reach at least ForkHeights['NON_CEASING_SC']
            currBlck = self.nodes[0].getblockcount()
            if currBlck < ForkHeights['NON_CEASING_SC']:
                self.nodes[0].generate(ForkHeights['NON_CEASING_SC'] - currBlck)

        ### Sidechain creation
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params(sc_name) if scversion < 2 else mcTest.generate_params(sc_name, keyrot = True)
        constant = generate_random_field_element_hex()
        cmdInput = {
            "version": scversion,
            "withdrawalEpochLength": epoch_length,
            "toaddress": address,
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": constant,
            "forwardTransferScFee": FT_SC_FEE,
            "mainchainBackwardTransferScFee": MBTR_SC_FEE
        }
        ret = self.nodes[0].sc_create(cmdInput)
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs_temp(cc('g',"Node 0 created a SC: ") + scid, self.nodes, DEBUG_MODE, color='n')

        ### Sidechain confirmation
        mark_logs_temp(f"Node 0 mines 1 block to confirm the sidechain", self.nodes, DEBUG_MODE, color='e')
        self.nodes[0].generate(1)
        self.sync_all()
        # Ensure that the SC is alive
        sc_info = self.nodes[0].getscinfo(scid)
        assert_equal(sc_info['items'][0]['state'], 'ALIVE')

        # Send some funds to node 2 to create a certificate
        mark_logs_temp("Node 0 sends some funds to nodes 1 and 2", self.nodes, DEBUG_MODE, color='g')
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 10.0)
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 20.0)
        self.nodes[0].generate(1)
        self.sync_all()

        # Mine blocks until reaching new epoch
        blocksToBeGen = sc_info['items'][0]['endEpochHeight'] - self.nodes[0].getblockcount() + 1
        mark_logs_temp(f"Node 0 mines {blocksToBeGen} blocks until a new epoch begins", self.nodes, DEBUG_MODE, color='e')
        self.nodes[0].generate(blocksToBeGen)
        self.sync_all()

        # node 2 sends a certificate to node 3 to keep the sc alive
        # this certificate has both ft and mbtr fees greater than those specified during sc creation
        malicious_ft_fees = Decimal('0.05')
        malicious_mbtr_fees = Decimal('0.07')
        quality = 10 if ceasable else 0
        addr_node3 = self.nodes[3].getnewaddress()
        epoch_number, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[2], epoch_length, not ceasable)
        proof = mcTest.create_test_proof(sc_name,
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         malicious_mbtr_fees,
                                         malicious_ft_fees,
                                         epoch_cum_tree_hash,
                                         prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                         constant       = constant,
                                         pks            = [addr_node3],
                                         amounts        = [bwt_amount])

        amount_cert_3 = [{"address": addr_node3, "amount": bwt_amount}]

        mark_logs_temp("Node 2 sends a certificate with increased fees", self.nodes, DEBUG_MODE, color='g')
        try:
            cert_epoch_0 = self.nodes[2].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_3, malicious_ft_fees, malicious_mbtr_fees, CERT_FEE)
            assert(len(cert_epoch_0) > 0)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs_temp(f"Send certificate failed with reason {errorString}", self.nodes, DEBUG_MODE, color='r')
            assert(False)
        mark_logs_temp(cc('e',"Node 2 mines 1 block to validate cert: ") + cert_epoch_0, self.nodes, DEBUG_MODE, color='n')
        self.nodes[2].generate(1)
        self.sync_all()

        ### Create a network split:
        ### 1. disconnect nodes 1 and 2, so that the the nets are nodes 0, 1 and 2, 3
        ### 2. in the first round, node 0 sends many small txs to sc; save the node balances
        ### 3. node 2 mines block
        ### 4. rejoin the network
        ### 5. mined block should be broadcasted to nodes 0 and 1, but transactions shouldn't
        ### This process should repeat until we enter epoch 2
        blocksToBeGen = self.nodes[0].getscinfo(scid)['items'][0]['endEpochHeight'] - self.nodes[0].getblockcount() + 1
        loopfirstround = True
        for _ in range(blocksToBeGen):
            # 1.
            mark_logs_temp(f"Net split", self.nodes, DEBUG_MODE, color='c')
            self.split_network()

            # 2.
            if loopfirstround:
                mark_logs_temp(f"Node 0 adds {NUM_TXS} transactions to nodes 0 and 1 mempools", self.nodes, DEBUG_MODE, color='g')
                initial_balances = [self.nodes[x].getbalance() for x in range(NUMB_OF_NODES)]
                ft_address_1 = self.nodes[1].getnewaddress() # = mc_return_address
                forwardTransferOuts = [{'toaddress': address, 'amount': Decimal(float(FT_SC_FEE) + 0.05), "scid": scid, "mcReturnAddress": ft_address_1}]
                for _ in range(NUM_TXS):
                    self.nodes[0].sc_send(forwardTransferOuts)
                    sync_mempools(self.nodes[:2])
                loopfirstround = False
                assert_greater_than(float(initial_balances[0]), float(self.nodes[0].getbalance()))  # and balance is unaffected
            else:
                assert_greater_than(float(self.nodes[0].getbalance()), float(initial_balances[0]))
            self.assert_txs_in_mempool(NUM_TXS)
            assert_equal(float(self.nodes[1].getbalance()), float(initial_balances[1]))

            # 3.
            mark_logs_temp("Node 2 mines 1 block", self.nodes, DEBUG_MODE, color='e')
            self.nodes[2].generate(1)
            sync_blocks(self.nodes[2:])
            sync_mempools(self.nodes[2:])
            self.assert_chain_height_difference(1)      # Test that the newly mined block is only on nodes 2 and 3

            # 4.
            mark_logs_temp(f"Net join", self.nodes, DEBUG_MODE, color='c')
            self.join_network()
            sync_blocks(self.nodes)

            # 5.
            self.assert_chain_height_difference(0)      # Make sure that block is propagated to nodes 0 and 1
            self.assert_txs_in_mempool(NUM_TXS)         # Make sure that txs are only in nodes 0 and 1 mempool
            assert_greater_than(float(self.nodes[0].getbalance()), float(initial_balances[0]))  # and balance is unaffected
            assert_equal(float(self.nodes[1].getbalance()), float(initial_balances[1]))

        # All the nodes are now in epoch 2; transaction should still be on nodes 0 and 1
        # Re do the same story to reach epoch 3
        ### 6. split the net again
        ### 7. geneate a new cert with even higher fees on node 2
        ### 8. rejoin the network and check block heights and txs
        ### 9. repeat the loop for advancing blocks until entering epoch 3
        # 6.
        mark_logs_temp(f"Net split", self.nodes, DEBUG_MODE, color='c')
        self.split_network()
        # 7.
        malicious_ft_fees = Decimal('0.07')
        malicious_mbtr_fees = Decimal('0.09')
        epoch_number, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[2], epoch_length, not ceasable)
        proof = mcTest.create_test_proof(sc_name,
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         malicious_mbtr_fees,
                                         malicious_ft_fees,
                                         epoch_cum_tree_hash,
                                         prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                         constant       = constant,
                                         pks            = [addr_node3],
                                         amounts        = [bwt_amount])

        amount_cert_3 = [{"address": addr_node3, "amount": bwt_amount}]

        mark_logs_temp("Node 2 sends a certificate with increased fees", self.nodes, DEBUG_MODE, color='g')
        try:
            cert_epoch_0 = self.nodes[2].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_3, malicious_ft_fees, malicious_mbtr_fees, CERT_FEE)
            assert(len(cert_epoch_0) > 0)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs_temp(f"Send certificate failed with reason {errorString}", self.nodes, DEBUG_MODE, color='r')
            assert(False)
        mark_logs_temp(cc('e',"Node 2 mines 1 block to validate cert: ") + cert_epoch_0, self.nodes, DEBUG_MODE, color='n')
        self.nodes[2].generate(1)
        sync_blocks(self.nodes[2:])
        sync_mempools(self.nodes[2:])
        self.assert_chain_height_difference(1)          # Test that the newly mined block is only on nodes 2 and 3
        # 8.
        mark_logs_temp(f"Net join", self.nodes, DEBUG_MODE, color='c')
        self.join_network()
        sync_blocks(self.nodes)
        self.assert_chain_height_difference(0)          # Make sure that block is propagated to nodes 0 and 1

        # 9.
        blocksToBeGen = self.nodes[0].getscinfo(scid)['items'][0]['endEpochHeight'] - self.nodes[0].getblockcount() + 1
        for _ in range(blocksToBeGen):
            # 1.
            mark_logs_temp(f"Net split", self.nodes, DEBUG_MODE, color='c')
            self.split_network()
            # 2.
            self.assert_txs_in_mempool(NUM_TXS)         # Make sure that txs are only in nodes 0 and 1 mempool
            assert_greater_than(float(self.nodes[0].getbalance()), float(initial_balances[0]))
            assert_equal(float(self.nodes[1].getbalance()), float(initial_balances[1]))
            # 3.
            mark_logs_temp("Node 2 mines a block", self.nodes, DEBUG_MODE, color='e')
            self.nodes[2].generate(1)
            sync_blocks(self.nodes[2:])
            sync_mempools(self.nodes[2:])
            self.assert_chain_height_difference(1)      # Test that the newly mined block is only on nodes 2 and 3
            # 4.
            mark_logs_temp(f"Net join", self.nodes, DEBUG_MODE, color='c')
            self.join_network()
            sync_blocks(self.nodes)
            # 5.
            self.assert_chain_height_difference(0)      # Make sure that block is propagated to nodes 0 and 1
            self.assert_txs_in_mempool(NUM_TXS)         # Make sure that txs are only in nodes 0 and 1 mempool
            assert_greater_than(float(self.nodes[0].getbalance()), float(initial_balances[0]))  # and balance is unaffected
            assert_equal(float(self.nodes[1].getbalance()), float(initial_balances[1]))

        ### Lastly
        ### 10. split the net again
        ### 11. geneate a new cert with even higher fees on node 2
        ### 12. mine a block on node 2 to confirm the cert
        ### 13. rejoin the net and sync everything
        # 10.
        mark_logs_temp(f"Net split", self.nodes, DEBUG_MODE, color='c')
        self.split_network()
        # 11.
        malicious_ft_fees = Decimal('0.1')
        malicious_mbtr_fees = Decimal('0.11')
        epoch_number, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[2], epoch_length, not ceasable)
        proof = mcTest.create_test_proof(sc_name,
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         malicious_mbtr_fees,
                                         malicious_ft_fees,
                                         epoch_cum_tree_hash,
                                         prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                         constant       = constant,
                                         pks            = [addr_node3],
                                         amounts        = [bwt_amount])

        amount_cert_3 = [{"address": addr_node3, "amount": bwt_amount}]

        mark_logs_temp("Node 2 sends a certificate with increased fees", self.nodes, DEBUG_MODE, color='g')
        try:
            cert_epoch_0 = self.nodes[2].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_3, malicious_ft_fees, malicious_mbtr_fees, CERT_FEE)
            assert(len(cert_epoch_0) > 0)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs_temp(f"Send certificate failed with reason {errorString}", self.nodes, DEBUG_MODE, color='r')
            assert(False)
        # 12.
        mark_logs_temp(cc('e',"Node 2 mines 1 block to validate cert: ") + cert_epoch_0, self.nodes, DEBUG_MODE, color='n')
        self.nodes[2].generate(1)
        sync_blocks(self.nodes[2:])
        sync_mempools(self.nodes[2:])
        self.assert_chain_height_difference(1)          # Test that the newly mined block is only on nodes 2 and 3

        # 13.
        mark_logs_temp(f"Net join", self.nodes, DEBUG_MODE, color='c')
        self.join_network()
        sync_blocks(self.nodes)

        ### Final test
        ### a. nodes are at the same height
        ### b. transactions are no longer in 0 and 1 mempools
        ### c. balances of nodes 0 and 1 have not changed
        # a.
        self.assert_chain_height_difference(0)          # Make sure that block is propagated to nodes 0 and 1
        # b.
        mark_logs_temp("Assert txs are no longer in mempool", self.nodes, DEBUG_MODE, color='y')
        self.assert_txs_in_mempool(0)                   # BOOM! Transactions should no longer be in mempool
        # c.
        assert_greater_than(float(self.nodes[0].getbalance()), float(initial_balances[0]))  # and balance is unaffected
        assert_equal(float(self.nodes[1].getbalance()), float(initial_balances[1]))
        ### If so, transactions have been removed

        # Final check for de/serialization of scFees list
        final_scfeesLen = len(self.nodes[0].getscinfo(scid)['items'][0]['scFees'])
        mark_logs("Checking persistence stopping and restarting nodes", self.nodes, DEBUG_MODE)
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)
        for i in range(NUMB_OF_NODES):
            assert_equal(final_scfeesLen, len(self.nodes[i].getscinfo(scid)['items'][0]['scFees']))

        return

    def run_test_non_ceasable(self, scversion, ceasable = True, old_version = True):
        # amounts
        creation_amount = Decimal("100")
        bwt_amount = Decimal("1.0")
        sc_name = "sc" + str(scversion) + str(ceasable)
        address = "dada"

        # Adjust epoch length according to sc specifications
        epoch_length = 0 if not ceasable else EPOCH_LENGTH

        if (self.firstRound):
            mark_logs_temp(f"Node 0 generates {ForkHeights['NON_CEASING_SC']} block", self.nodes, DEBUG_MODE, color='e')
            self.nodes[0].generate(ForkHeights['NON_CEASING_SC'])
            self.sync_all()
            self.firstRound = False

        ### Sidechain creation
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params(sc_name, keyrot = True)
        constant = generate_random_field_element_hex()
        cmdInput = {
            "version": scversion,
            "withdrawalEpochLength": epoch_length,
            "toaddress": address,
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": constant,
            "forwardTransferScFee": FT_SC_FEE,
            # "mainchainBackwardTransferScFee": MBTR_SC_FEE
        }
        ret = self.nodes[0].sc_create(cmdInput)
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs_temp(cc('g',"Node 0 created a SC: ") + scid, self.nodes, DEBUG_MODE, color='n')

        ### Sidechain confirmation
        mark_logs_temp(f"Node 0 generating 1 block to confirm the sidechain", self.nodes, DEBUG_MODE, color='e')
        self.nodes[0].generate(1)
        self.sync_all()
        # Ensure that the SC is alive
        sc_info = self.nodes[0].getscinfo(scid)
        assert_equal(sc_info['items'][0]['state'], 'ALIVE')

        # Send some funds to node 2 to create a certificate and
        # generate a few blocks for allowing correct block referencing
        mark_logs_temp("Node 0 sends some funds to nodes 1 and 2", self.nodes, DEBUG_MODE, color='g')
        self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 10.0)
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), 20.0)
        self.nodes[0].generate(6)
        self.sync_all()

        ### Unlike for ceasing sidechains, we can already create a split:
        ### 1. disconnect nodes 1 and 2, so that the the nets are nodes 0, 1 and 2, 3
        ### 2. node 0 sends many small txs to sc; save the node balances
        ### 3. node 2 generate a certificate for epoch 0
        ### 4. node 2 mines a block to validate the certificate and ends epoch 0
        ### 5. rejoin the network
        ### 6. mined block should be broadcasted to nodes 0 and 1, but transactions shouldn't
        # 1.
        mark_logs_temp(f"Net split", self.nodes, DEBUG_MODE, color='c')
        self.split_network()

        # 2.
        initial_balances = [self.nodes[x].getbalance() for x in range(NUMB_OF_NODES)]
        ft_address_1 = self.nodes[1].getnewaddress() # = mc_return_address
        forwardTransferOuts = [{'toaddress': address, 'amount': Decimal(float(FT_SC_FEE) + 0.05), "scid": scid, "mcReturnAddress": ft_address_1}]
        for _ in range(NUM_TXS):
            self.nodes[0].sc_send(forwardTransferOuts)
            sync_mempools(self.nodes[:2])
        assert_greater_than(float(initial_balances[0]), float(self.nodes[0].getbalance()))  # balance is unaffected
        self.assert_txs_in_mempool(NUM_TXS)
        assert_equal(float(self.nodes[1].getbalance()), float(initial_balances[1]))  # balance is unaffected
        mark_logs_temp(f"Node 0 added {NUM_TXS} transactions to nodes 0 and 1 mempools", self.nodes, DEBUG_MODE, color='c')

        # 3.
        # node 2 send a ceritificate to node 3
        # this certificate has both ft and mbtr fees greater than those specified during sc creation
        malicious_ft_fees = Decimal('0.7')
        malicious_mbtr_fees = Decimal('0')
        quality = 0
        addr_node3 = self.nodes[3].getnewaddress()
        epoch_number, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[2], epoch_length, True, self.nodes[2].getblockcount() - 2)
        proof = mcTest.create_test_proof(sc_name,
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         malicious_mbtr_fees,
                                         malicious_ft_fees,
                                         epoch_cum_tree_hash,
                                         prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                         constant       = constant,
                                         pks            = [addr_node3],
                                         amounts        = [bwt_amount])

        amount_cert_3 = [{"address": addr_node3, "amount": bwt_amount}]

        mark_logs_temp("Node 2 sends a certificate with increased fees", self.nodes, DEBUG_MODE, color='g')
        try:
            cert_epoch_0 = self.nodes[2].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_3, malicious_ft_fees, malicious_mbtr_fees, CERT_FEE)
            assert(len(cert_epoch_0) > 0)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs_temp(f"Send certificate failed with reason {errorString}", self.nodes, DEBUG_MODE, color='r')
            assert(False)

        # 4.
        mark_logs_temp(cc('e',"Node 2 mines 1 block to validate cert: ") + cert_epoch_0, self.nodes, DEBUG_MODE, color='n')
        self.nodes[2].generate(1)
        sync_blocks(self.nodes[2:])
        sync_mempools(self.nodes[2:])
        self.assert_chain_height_difference(1)      # Test that the newly mined block is only on nodes 2 and 3

        # 5.
        mark_logs_temp(f"Net join", self.nodes, DEBUG_MODE, color='c')
        self.join_network()
        sync_blocks(self.nodes)

        # 6.
        self.assert_chain_height_difference(0)      # Make sure that block is propagated to nodes 0 and 1
        self.assert_txs_in_mempool(NUM_TXS)         # Make sure that txs are only in nodes 0 and 1 mempool
        assert_greater_than(float(self.nodes[0].getbalance()), float(initial_balances[0]))  # and balance is unaffected
        assert_equal(float(self.nodes[1].getbalance()), float(initial_balances[1]))

        ### Repeat the same process, minus tx generation
        # 1.
        mark_logs_temp(f"Net split", self.nodes, DEBUG_MODE, color='c')
        self.split_network()
        # 3.
        # node 2 sends a ceritificate to node 3
        # this certificate has ftr fee greater than those specified during sc creation
        malicious_ft_fees = Decimal('0.1')
        addr_node3 = self.nodes[3].getnewaddress()
        epoch_number, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[2], epoch_length, True, self.nodes[2].getblockcount())
        proof = mcTest.create_test_proof(sc_name,
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         malicious_mbtr_fees,
                                         malicious_ft_fees,
                                         epoch_cum_tree_hash,
                                         prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                         constant       = constant,
                                         pks            = [addr_node3],
                                         amounts        = [bwt_amount])

        amount_cert_3 = [{"address": addr_node3, "amount": bwt_amount}]

        mark_logs_temp("Node 2 sends a certificate with increased fees", self.nodes, DEBUG_MODE, color='g')
        try:
            cert_epoch_0 = self.nodes[2].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amount_cert_3, malicious_ft_fees, malicious_mbtr_fees, CERT_FEE)
            assert(len(cert_epoch_0) > 0)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs_temp(f"Send certificate failed with reason {errorString}", self.nodes, DEBUG_MODE, color='r')
            assert(False)

        # 4.
        mark_logs_temp(cc('e',"Node 2 mines 1 block to validate cert: ") + cert_epoch_0, self.nodes, DEBUG_MODE, color='n')
        self.nodes[2].generate(1)
        sync_blocks(self.nodes[2:])
        sync_mempools(self.nodes[2:])
        self.assert_chain_height_difference(1)      # Test that the newly mined block is only on nodes 2 and 3

        # 5.
        mark_logs_temp(f"Net join", self.nodes, DEBUG_MODE, color='c')
        self.join_network()
        sync_blocks(self.nodes)
        self.assert_chain_height_difference(0)      # Make sure that block is propagated to nodes 0 and 1

        # Before the update, txs should be removed here as soon as we sync the split
        if old_version:
            # 6.
            mark_logs_temp("Assert txs are no longer in mempool", self.nodes, DEBUG_MODE, color='y')
            self.assert_txs_in_mempool(0)               # BOOM! Transactions should no longer be in mempool
            assert_greater_than(float(self.nodes[0].getbalance()), float(initial_balances[0]))  # and balance is unaffected
            assert_equal(float(self.nodes[1].getbalance()), float(initial_balances[1]))

        # Instead, after the update, we should be able to submit 9 more certs before
        # making the transactions disappear
        else:
            incremental_fees = 0.1
            for i in range(9):
                self.assert_txs_in_mempool(NUM_TXS)         # Make sure that txs are only in nodes 0 and 1 mempool

                mark_logs_temp(f"Net split", self.nodes, DEBUG_MODE, color='c')
                self.split_network()

                incremental_fees = incremental_fees + 0.05
                malicious_ft_fees = Decimal(str(incremental_fees))
                addr_node3 = self.nodes[3].getnewaddress()
                epoch_number, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[2], epoch_length, True, self.nodes[2].getblockcount())
                proof = mcTest.create_test_proof(sc_name,
                                                 scid_swapped,
                                                 epoch_number,
                                                 quality,
                                                 malicious_mbtr_fees,
                                                 malicious_ft_fees,
                                                 epoch_cum_tree_hash,
                                                 prev_cert_hash = prev_cert_hash if scversion >= 2 else None,
                                                 constant       = constant,
                                                 pks            = [addr_node3],
                                                 amounts        = [bwt_amount])

                amount_cert_3 = [{"address": addr_node3, "amount": bwt_amount}]

                mark_logs_temp("Node 2 sends a certificate with increased fees", self.nodes, DEBUG_MODE, color='g')
                try:
                    cert_epoch_0 = self.nodes[2].sc_send_certificate(scid, epoch_number, quality,
                        epoch_cum_tree_hash, proof, amount_cert_3, malicious_ft_fees, malicious_mbtr_fees, CERT_FEE)
                    assert(len(cert_epoch_0) > 0)
                except JSONRPCException as e:
                    errorString = e.error['message']
                    mark_logs_temp(f"Send certificate failed with reason {errorString}", self.nodes, DEBUG_MODE, color='r')
                    assert(False)

                mark_logs_temp(cc('e',"Node 2 mines 1 block to validate cert: ") + cert_epoch_0, self.nodes, DEBUG_MODE, color='n')
                self.nodes[2].generate(1)
                sync_blocks(self.nodes[2:])
                sync_mempools(self.nodes[2:])
                self.assert_chain_height_difference(1)      # Test that the newly mined block is only on nodes 2 and 3

                mark_logs_temp(f"Net join", self.nodes, DEBUG_MODE, color='c')
                self.join_network()
                sync_blocks(self.nodes)
                self.assert_chain_height_difference(0)      # Make sure that block is propagated to nodes 0 and 1

            # Finally, we are over the minScFeesBlocksUpdate border
            mark_logs_temp("Assert txs are no longer in mempool", self.nodes, DEBUG_MODE, color='y')
            self.assert_txs_in_mempool(0)                   # BOOM! Transactions should no longer be in mempool
            assert_greater_than(float(self.nodes[0].getbalance()), float(initial_balances[0]))  # and balance is unaffected
            assert_equal(float(self.nodes[1].getbalance()), float(initial_balances[1]))

            # Final check for de/serialization of scFees list
            scFeesList = self.nodes[0].getscinfo(scid)['items'][0]['scFees']
            final_scfeesLen = len(scFeesList)
            mark_logs("Checking persistence stopping and restarting nodes", self.nodes, DEBUG_MODE)
            stop_nodes(self.nodes)
            wait_bitcoinds()
            self.setup_network(False)
            for i in range(NUMB_OF_NODES):
                assert_equal(final_scfeesLen, len(self.nodes[i].getscinfo(scid)['items'][0]['scFees']))

        return


    def run_test(self):
        mark_logs_temp("*** SC version 0", self.nodes, DEBUG_MODE, color='b')
        self.run_test_with_scversion(0)
        mark_logs_temp("*** SC version 2 - ceasable SC", self.nodes, DEBUG_MODE, color='b')
        self.run_test_with_scversion(2, True)
        mark_logs_temp("*** SC version 2 - non-ceasable SC", self.nodes, DEBUG_MODE, color='b')
        self.run_test_non_ceasable(2, False, False)

if __name__ == '__main__':
    provaFee().main()
