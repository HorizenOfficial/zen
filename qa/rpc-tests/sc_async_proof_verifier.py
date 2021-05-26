#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, connect_nodes_bi, assert_true, assert_false, mark_logs, \
    wait_bitcoinds, stop_nodes, get_epoch_data, sync_mempools, sync_blocks, \
    disconnect_nodes, advance_epoch

from test_framework.mc_test.mc_test import *

from decimal import Decimal
import pprint
import time
import codecs

NUMB_OF_NODES = 2
DEBUG_MODE = 1
EPOCH_LENGTH = 6
CERT_FEE = Decimal('0.0001')

# The delay [seconds] between batch verifications of the async proof verifier.
# This variable must be aligned with BATCH_VERIFICATION_MAX_DELAY in asyncproofverifier.h.
BATCH_VERIFICATION_MAX_DELAY = 5

# The delay [seconds] to wait after submitting a certificate or CSW transaction
# so that it can be expected to have been batch verified asynchronously and
# added to the memory pool.
MEMPOOL_LONG_WAIT_TIME = BATCH_VERIFICATION_MAX_DELAY * 2

# The delay [seconds] to wait after submitting a normal transaction
# so that it can be expected to have been added to the memory pool while any
# concurrent certificate or CSW transaction should steel be pending.
MEMPOOL_SHORT_WAIT_TIME = 2

assert_true(MEMPOOL_SHORT_WAIT_TIME < BATCH_VERIFICATION_MAX_DELAY)
assert_true(MEMPOOL_LONG_WAIT_TIME > BATCH_VERIFICATION_MAX_DELAY)

# Create one-input, one-output, no-fee transaction:
class AsyncProofVerifierTest(BitcoinTestFramework):

    def swap_bytes(self, input_buf):
        return codecs.encode(codecs.decode(input_buf, 'hex')[::-1], 'hex').decode()

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[["-sccoinsmaturity=0", '-logtimemicros=1', '-debug=sc', '-debug=py',
                                             '-debug=mempool', '-debug=net', '-debug=bench'],
                                             ["-sccoinsmaturity=0", '-logtimemicros=1', '-debug=sc', '-debug=py',
                                             '-debug=mempool', '-debug=net', '-debug=bench']])
                                             # Skip proof verification for the last node
                                             # TODO: enable this part after the integration with the updated proof verification system.
                                             #  ["-skipscproof", "-sccoinsmaturity=0", '-logtimemicros=1', '-debug=sc', '-debug=py',
                                             #  '-debug=mempool', '-debug=net', '-debug=bench']])

        connect_nodes_bi(self.nodes, 0, 1)
        # TODO: connect the last node after the integration with the updated proof verification system.
        # connect_nodes_bi(self.nodes, 1, 2)
        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        Verify that the async proof verifier for sidechain proves works as expected.
        '''

        # Prepare some coins 
        self.nodes[0].generate(220)
        self.sync_all()
        prev_epoch_hash = self.nodes[0].getbestblockhash()

        sc_address = "0000000000000000000000000000000000000000000000000000000000000abc"
        sc_epoch_len = EPOCH_LENGTH
        sc_cr_amount = Decimal('12.00000000')

        certMcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        cswMcTest  = CSWTestUtils(self.options.tmpdir, self.options.srcdir)

        # generate wCertVk and constant
        vk = certMcTest.generate_params("sc")
        csw_vk = cswMcTest.generate_params("sc")
        constant = generate_random_field_element_hex()

        sc_cr = []
        sc_cr.append({
            "epoch_length": sc_epoch_len,
            "amount": sc_cr_amount,
            "address": sc_address,
            "wCertVk": vk,
            "wCeasedVk": csw_vk,
            "constant": constant
        })

        rawtx = self.nodes[0].createrawtransaction([], {}, [], sc_cr)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sig_raw_tx = self.nodes[0].signrawtransaction(funded_tx['hex'])
        final_raw_tx = self.nodes[0].sendrawtransaction(sig_raw_tx['hex'])
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(final_raw_tx, 1)
        scid = decoded_tx['vsc_ccout'][0]['scid']
        print "        scid :", scid
        scid_swapped = self.swap_bytes(scid)
        print "scid swapped:", scid_swapped
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)
        print

        # Advance one epoch
        mark_logs("\nLet 1 epoch pass by...", self.nodes, DEBUG_MODE)

        cert, epoch_number = advance_epoch(
            certMcTest, self.nodes[0], self.sync_all,
            scid, prev_epoch_hash, "sc", constant, sc_epoch_len)

        mark_logs("\n==> certificate for SC epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        # Check that the certificate is in the mempool
        mark_logs("Check certificate is in mempool...", self.nodes, DEBUG_MODE)
        assert_true(cert in self.nodes[0].getrawmempool())
        assert_true(cert in self.nodes[1].getrawmempool())

        # Generate blocks to reach the next epoch
        mark_logs("\nLet another epoch pass by...", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(sc_epoch_len)
        self.sync_all()

        # Check that the certificate is not in the mempool anymore
        mark_logs("Check certificate is not in mempool anymore...", self.nodes, DEBUG_MODE)
        assert_false(cert in self.nodes[0].getrawmempool())
        assert_false(cert in self.nodes[1].getrawmempool())

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], sc_epoch_len)
        cert_quality = 1
        cert_fee = Decimal("0.00001")
        ft_fee = 0
        mbtr_fee = 0

        # TODO: enable this section of the test after the integration with the updated proof verification system
        # (MC Crypto Lib and CCTP Lib) since currently there is no way of providing a proof that fails the verification.
        #
        # TODO: to make this section work, it is needed to add a third node to the network.
        #
        # # Manually create a certificate with invalid proof to test the ban mechanism
        # mark_logs("\nTest the node ban mechanism by sending a certificate with invalid proof", self.nodes, DEBUG_MODE)

        # # Create an invalid proof by providing the wrong epoch_number
        # proof = mc_test.create_test_proof("sc", epoch_number + 1, epoch_block_hash, prev_epoch_hash, 1, constant, [], [])

        # try:
        #     # The send_certificate call must be ok since the proof verification is disabled on node 2
        #     cert = self.nodes[2].send_certificate(scid, epoch_number, cert_quality, epoch_block_hash, epoch_cum_tree_hash,
        #                                           proof, [], ft_fee, mbtr_fee, cert_fee)
        # except JSONRPCException, e:
        #     error_string = e.error['message']
        #     print "Send certificate failed with reason {}".format(error_string)
        #     assert(False)

        # mark_logs("\n==> certificate for SC epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        # # Check that the certificate is in node 2 mempool
        # assert_true(cert in self.nodes[2].getrawmempool())

        # # Wait until the other nodes process the certificate relayed by node 2
        # time.sleep(MEMPOOL_LONG_WAIT_TIME)

        # # Check that the other nodes didn't accept the certificate containing the wrong proof
        # assert_false(cert in self.nodes[0].getrawmempool())
        # assert_false(cert in self.nodes[1].getrawmempool())

        # # Check that the other nodes banned the node 2
        # assert_equal(len(self.nodes[0].listbanned()), 1)
        # assert_equal(len(self.nodes[1].listbanned()), 1)

        # # Remove node 2 from banned list
        # self.nodes[0].clearbanned()
        # self.nodes[1].clearbanned()

        # Create the valid proof
        proof = certMcTest.create_test_proof(
                "sc", epoch_number, cert_quality, mbtr_fee, ft_fee, constant, epoch_cum_tree_hash, [], [])
        #print "proof = ", proof

        try:
            cert = self.nodes[0].send_certificate(scid, epoch_number, cert_quality, epoch_cum_tree_hash,
                                                  proof, [], ft_fee, mbtr_fee, cert_fee)
        except JSONRPCException, e:
            error_string = e.error['message']
            print "Send certificate failed with reason {}".format(error_string)
            assert(False)

        mark_logs("\n==> certificate for SC epoch {} {}".format(epoch_number, cert), self.nodes, DEBUG_MODE)

        # Create a normal (not sidechain) transaction
        mark_logs("\nCreate a normal tx", self.nodes, DEBUG_MODE)
        list_unspent = self.nodes[0].listunspent()
        utxo = list_unspent[0]
        inputs = [{'txid' : utxo['txid'], 'vout' : utxo['vout']}]
        outputs = {self.nodes[0].getnewaddress() : 1.0}
        normal_raw_tx = self.nodes[0].createrawtransaction(inputs, outputs)
        normal_funded_tx = self.nodes[0].fundrawtransaction(normal_raw_tx)
        normal_sig_raw_tx = self.nodes[0].signrawtransaction(normal_funded_tx['hex'], None, None, "NONE")
        normal_final_raw_tx = self.nodes[0].sendrawtransaction(normal_sig_raw_tx['hex'])
        mark_logs("sent csw tx {}".format(normal_final_raw_tx), self.nodes, DEBUG_MODE)

        # Wait for the transaction to be sent to node 1
        mark_logs("\nWait for the transaction to be sent to node 1", self.nodes, DEBUG_MODE)
        time.sleep(MEMPOOL_SHORT_WAIT_TIME)

        # Check that the normal transaction is in the mempool
        mark_logs("Check normal tx is in mempool...", self.nodes, DEBUG_MODE)
        assert_true(normal_final_raw_tx in self.nodes[1].getrawmempool())

        # Check that the certificate is not in the mempool yet
        mark_logs("Check certificate is not in mempool...", self.nodes, DEBUG_MODE)
        assert_false(cert in self.nodes[1].getrawmempool())

        # Wait for the certificate proof verification to complete
        mark_logs("Wait for the certificate proof verification to complete...", self.nodes, DEBUG_MODE)
        self.sync_all()

        # Check that the certificate is in the mempool
        mark_logs("Check certificate tx is in mempool...", self.nodes, DEBUG_MODE)
        assert_true(cert in self.nodes[1].getrawmempool())

        # Mine one block to have last cert in chain
        mark_logs("\nNode0 generates 1 block confirming last cert", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Let SC cease... ", self.nodes, DEBUG_MODE)

        nbl = int(sc_epoch_len * 1.5)
        mark_logs("Node0 generates {} blocks".format(nbl), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(nbl)
        self.sync_all()

        # Check the sidechain is really ceased
        ret = self.nodes[0].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['state'], "CEASED")

        # Check the sidechain has the expected balance
        sc_bal = self.nodes[0].getscinfo(scid, False, False)['items'][0]['balance']
        assert_equal(sc_bal, sc_cr_amount)

        # create a tx with 3 CSW for sc
        mark_logs("\nCreate 3 CSWs in a tx withdrawing half the sc balance... ", self.nodes, DEBUG_MODE)

        # CSW sender MC address, in taddress and pub key hash formats
        csw_mc_address = self.nodes[0].getnewaddress()
        pkh_mc_address = self.nodes[0].validateaddress(csw_mc_address)['pubkeyhash']

        sc_csw_amount = (sc_bal / 2) / 3
        null_1 = generate_random_field_element_hex()
        null_2 = generate_random_field_element_hex()
        null_3 = generate_random_field_element_hex()

        act_cert_data = self.nodes[0].getactivecertdatahash(scid)['certDataHash']
        ceasingCumScTxCommTree = self.nodes[0].getceasingcumsccommtreehash(scid)['ceasingCumScTxCommTree']
        pprint.pprint(act_cert_data)

        sc_proof = cswMcTest.create_test_proof(
                "sc", sc_csw_amount, str(scid_swapped), pkh_mc_address, ceasingCumScTxCommTree, act_cert_data) 
        #print "sc_proof =", sc_proof

        sc_csws = [
            {
                "amount": sc_csw_amount,
                "senderAddress": csw_mc_address,
                "scId": scid,
                "epoch": 0,
                "nullifier": null_1,
                "activeCertData": act_cert_data,
                "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
                "scProof": sc_proof
            },
            {
                "amount": sc_csw_amount,
                "senderAddress": csw_mc_address,
                "scId": scid,
                "epoch": 0,
                "nullifier": null_2,
                "activeCertData": act_cert_data,
                "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
                "scProof": sc_proof
            },
            {
                "amount": sc_csw_amount,
                "senderAddress": csw_mc_address,
                "scId": scid,
                "epoch": 0,
                "nullifier": null_3,
                "activeCertData": act_cert_data,
                "ceasingCumScTxCommTree": ceasingCumScTxCommTree,
                "scProof": sc_proof
            }
        ]

        # recipient MC address
        taddr = self.nodes[1].getnewaddress()
        sc_csw_tx_outs = {taddr: Decimal(sc_csw_amount * 4)}

        rawtx = self.nodes[0].createrawtransaction([], sc_csw_tx_outs, sc_csws)
        funded_tx = self.nodes[0].fundrawtransaction(rawtx)
        sig_raw_tx = self.nodes[0].signrawtransaction(funded_tx['hex'], None, None, "NONE")
        final_raw_tx = self.nodes[0].sendrawtransaction(sig_raw_tx['hex'])
        mark_logs("sent csw tx {}".format(final_raw_tx), self.nodes, DEBUG_MODE)

        # The CSW prooves will take some time to be verified
        mark_logs("Check CSW tx is not in mempool...", self.nodes, DEBUG_MODE)
        assert_false(final_raw_tx in self.nodes[1].getrawmempool())

        # Create a normal (not sidechain) transaction
        mark_logs("\nCreate a normal tx", self.nodes, DEBUG_MODE) 
        list_unspent = self.nodes[0].listunspent()

        # Take a random UTXO (to avoid double spending conflicts with the previous CSW transaction)
        utxo = list_unspent[10]
        inputs = [ {'txid' : utxo['txid'], 'vout' : utxo['vout']}]
        outputs = {self.nodes[0].getnewaddress() : 1.0}
        normal_raw_tx = self.nodes[0].createrawtransaction(inputs, outputs)
        normal_funded_tx = self.nodes[0].fundrawtransaction(normal_raw_tx)
        normal_sig_raw_tx = self.nodes[0].signrawtransaction(normal_funded_tx['hex'], None, None, "NONE")
        normal_final_raw_tx = self.nodes[0].sendrawtransaction(normal_sig_raw_tx['hex'])
        mark_logs("sent csw tx {}".format(normal_final_raw_tx), self.nodes, DEBUG_MODE)

        # Wait for the transaction to be sent to node 1
        mark_logs("\nWait for the transaction to be sent to node 1", self.nodes, DEBUG_MODE)
        time.sleep(MEMPOOL_SHORT_WAIT_TIME)

        # Check that the normal transaction is in the mempool
        mark_logs("Check normal tx is in mempool...", self.nodes, DEBUG_MODE)
        assert_true(normal_final_raw_tx in self.nodes[1].getrawmempool())

        # Check that the CSW transaction is not in the mempool yet
        mark_logs("Check CSW tx is not in mempool...", self.nodes, DEBUG_MODE)
        assert_false(final_raw_tx in self.nodes[1].getrawmempool())

        # Wait for the CSW proof verification to complete
        mark_logs("Wait for the CSW proof verification to complete...", self.nodes, DEBUG_MODE)
        self.sync_all()

        # Check that the CSW transaction is in the mempool
        mark_logs("Check CSW tx is in mempool...", self.nodes, DEBUG_MODE)
        assert_true(final_raw_tx in self.nodes[1].getrawmempool())

        # Get the current async proof verifier statistics
        node0_initial_stats = self.nodes[0].getproofverifierstats()
        node1_initial_stats = self.nodes[1].getproofverifierstats()

        # Generate one block containing the last CSW transaction (currently in the mempool)
        mark_logs("Generate one block...", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # Check that the CSW transaction has been removed from the mempool
        mark_logs("Check that the CSW transaction has been removed from the mempool...", self.nodes, DEBUG_MODE)
        assert_false(final_raw_tx in self.nodes[0].getrawmempool())
        assert_false(final_raw_tx in self.nodes[1].getrawmempool())

        # Check that the async proof verifier has not been called by 'ConnectBlock()'
        # by comparing its current statistics with the initial ones.
        # This way we are also sure that it has not been called by 'CreateNewBlock()'
        # since we generated a block to include the CSW transaction.
        assert_equal(node0_initial_stats, self.nodes[0].getproofverifierstats())
        assert_equal(node1_initial_stats, self.nodes[1].getproofverifierstats())

        # Disconnect one block
        mark_logs("Disconnect one block...", self.nodes, DEBUG_MODE)
        current_height = self.nodes[0].getblockcount()
        last_block_hash = self.nodes[0].getbestblockhash()
        self.nodes[0].invalidateblock(last_block_hash)
        self.nodes[1].invalidateblock(last_block_hash)
        self.sync_all()

        assert_equal(self.nodes[0].getblockcount(), current_height - 1)
        assert_equal(self.nodes[1].getblockcount(), current_height - 1)

        # Check that the async proof verifier has not been called by 'DisconnectTip()'
        # by comparing its current statistics with the initial ones.
        assert_equal(node0_initial_stats, self.nodes[0].getproofverifierstats())
        assert_equal(node1_initial_stats, self.nodes[1].getproofverifierstats())


if __name__ == '__main__':
    AsyncProofVerifierTest().main()
