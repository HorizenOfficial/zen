#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.authproxy import JSONRPCException
from test_framework.util import initialize_chain_clean, get_epoch_data, swap_bytes, \
    start_nodes, connect_nodes_bi, mark_logs
from test_framework.mc_test.mc_test import *
from decimal import Decimal

DEBUG_MODE    = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH  = 0

FT_SC_FEE      = Decimal('0')
MBTR_SC_FEE    = Decimal('0')
CERT_FEE       = Decimal('0.00015')
PARAMS_NAME = "sc"

# This script reproduces an issue introduced with non-ceasable sidechains, where conflicting
# certificates and transactions may be submitted in the mempool, creating undefined behaviors.
# Briefly, the test executes the following steps:
# - create a non-ceasable sidechain
# - submit a certificate and include that in a block
# - submit a transaction explicitly spending one of the certificate output; do not include
#   this transaction in a block
# - Invalidate the block containing the certificate; the certificate gets pushed back to the
#   mempool, creating a conflict with the transaction
# - Under normal circumstances, the transaction should be purged from the mempool

class non_ceasing_sc_mempool_conflict(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-debug=zendoo_mc_cryptolib', '-scproofqueuesize=0', '-logtimemicros=1']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = False
        self.sync_all()

    def try_send_certificate(self, node_idx, scid, epoch_number, quality, ref_height, mbtr_fee, ft_fee, bt, expect_failure, failure_reason=None):
        scid_swapped = str(swap_bytes(scid))
        _, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[node_idx], 0, True, ref_height)
        proof = self.mcTest.create_test_proof(PARAMS_NAME,
                                              scid_swapped,
                                              epoch_number,
                                              quality,
                                              mbtr_fee,
                                              ft_fee,
                                              epoch_cum_tree_hash,
                                              prev_cert_hash,
                                              constant = self.constant,
                                              pks      = [bt["address"]],
                                              amounts  = [bt["amount"]])

        mark_logs("Node {} sends cert of quality {} epoch {} ref {} with bwt of {}, expecting {}".format(node_idx, quality, epoch_number, ref_height, bt["amount"], "failure" if expect_failure else "success"), self.nodes, DEBUG_MODE, color='c')
        try:
            cert = self.nodes[node_idx].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, [bt], ft_fee, mbtr_fee, CERT_FEE)
            assert(not expect_failure)
            mark_logs("Sent certificate {}".format(cert), self.nodes, DEBUG_MODE, color='g')
            return cert 
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE, color='y')
            assert(expect_failure)
            if failure_reason is not None:
                assert(failure_reason in errorString)
            return None

    def run_test(self):

        # General sc / cert variables
        creation_amount = Decimal("50")
        bwt_amount_1    = Decimal("3")
        address         = "dada"
        btw_address     = self.nodes[0].getnewaddress()

        ##############################################
        # Preliminaries:
        # - reach non-ceasing sc fork
        # - create a new sc
        mark_logs("Node 0 generates {} block".format(ForkHeights['NON_CEASING_SC']), self.nodes, DEBUG_MODE, color='c')
        self.nodes[0].generate(ForkHeights['NON_CEASING_SC'])
        self.sync_all()

        # generate wCertVk and constant
        self.mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = self.mcTest.generate_params(PARAMS_NAME, keyrot=True)
        self.constant = generate_random_field_element_hex()

        # generate sidechain
        cmdInput = {
            "version": 2,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "toaddress": address,
            "amount": creation_amount,
            "wCertVk": vk,
            "constant": self.constant,
            'forwardTransferScFee': FT_SC_FEE,
            'mainchainBackwardTransferScFee': MBTR_SC_FEE
        }

        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        mark_logs("Node 0 created the SC {} spending {} coins via tx {}.".format(scid, creation_amount, creating_tx), self.nodes, DEBUG_MODE, color='c')

        mark_logs("Node 0 confirms sc creation generating 2 blocks", self.nodes, DEBUG_MODE, color='e')
        self.nodes[0].generate(2)[0]
        sc_creation_height = self.nodes[0].getblockcount() - 1
        self.sync_all()
        print()

        ##############################################
        # Step one:
        # - send the certificate for epoch 0 - not really mandatory
        epoch_number = 0
        ref_quality = 2
        ref_height = sc_creation_height
        amount_cert_0 = {"address": btw_address, "amount": bwt_amount_1}
        cert_0 = self.try_send_certificate(0, scid, epoch_number, ref_quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amount_cert_0, False)

        ##############################################
        # Step two:
        # - mine a few blocks to advance epoch
        mark_logs("Node 0 confirms the certificate submission generating 2 blocks. We also change epoch 0 -> 1", self.nodes, DEBUG_MODE, color='e')
        self.nodes[0].generate(5)
        self.sync_all()
        print()

        ##############################################
        # Step three:
        # - send certificate for epoch 1
        # - mine one block
        amounts = {"address": btw_address, "amount": bwt_amount_1}

        epoch_number = 1
        ref_quality = 2
        ref_height = self.nodes[0].getblockcount() - 2
        cert_1 = self.try_send_certificate(0, scid, epoch_number, ref_quality, ref_height, MBTR_SC_FEE, FT_SC_FEE, amounts, False)
        self.sync_all()

        block_to_revert = self.nodes[0].generate(1)
        self.sync_all()
        print()

        ##############################################
        # Step four:
        # - send a transaction spending the BTW out
        mark_logs("Node 0 submits a transaction that spends the BTW output", self.nodes, DEBUG_MODE, color='c')
        btw = self.nodes[0].gettransaction(cert_1)['details'][0] # Not mandatory, but this is the btw
        cert_tx_id = self.nodes[0].gettransaction(cert_1)['txid']
        amount          = Decimal('2')
        inputs          = [{"txid": cert_tx_id, "vout": 1}]
        outputs         = {self.nodes[0].getnewaddress(): amount}
        rawTx           = self.nodes[0].createrawtransaction(inputs, outputs)
        tx_rawtx        = self.nodes[0].signrawtransaction(rawTx)
        tx_spending_btw = self.nodes[0].sendrawtransaction(tx_rawtx['hex'], True)

        self.sync_all()     # Send the transaction to all the nodes
        print()

        ##############################################
        # Step five:
        # - invalidate the block
        mark_logs("Node 0 invalidates the block containing the certificate, sending it back to the mempool, along with the transaction.", self.nodes, DEBUG_MODE, color='c')
        self.nodes[0].invalidateblock(block_to_revert[0])
        mark_logs("Node 0 should be fine", self.nodes, DEBUG_MODE, color='g')
        rawmempool = self.nodes[0].getrawmempool()
        print()

        # Check that the tx is no longer in the mempool, and the cert is
        mark_logs("The tx which spends the btw should no longer be in the mempool", self.nodes, DEBUG_MODE, color='g')
        assert(tx_spending_btw not in rawmempool)
        mark_logs("The last certificate should be back in the mempool", self.nodes, DEBUG_MODE, color='g')
        assert(cert_tx_id in rawmempool)


if __name__ == '__main__':
    non_ceasing_sc_mempool_conflict().main()