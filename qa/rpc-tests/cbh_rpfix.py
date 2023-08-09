#!/usr/bin/env python3

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_equal, assert_greater_than, initialize_chain_clean, \
    start_nodes, start_node, connect_nodes, stop_node, stop_nodes, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, p2p_port, check_json_precision
from test_framework.util import swap_bytes
from decimal import Decimal
from test_framework.blocktools import create_tampered_rawtx_cbh, MODE_HEIGHT, MODE_SWAP_ARGS, MODE_NON_MIN_ENC
import os
import pprint

NUMB_OF_NODES = 3

# the scripts will not be checked if we have more than this depth of referenced block
FINALITY_SAFE_DEPTH = 150

# 0 means do not check any minimum age for referenced blocks in scripts
FINALITY_MIN_AGE = 75

# mainchain uses this value for targeting a blockhash in cbh script starting from chain tip backwards
CBH_DELTA_HEIGHT = 300

class cbh_rpfix(BitcoinTestFramework):
    
    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False, minAge=FINALITY_MIN_AGE):
        self.nodes = []
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
            extra_args = [
                ["-logtimemicros", "-debug=py", "-debug=mempool", "-debug=cbh", "-cbhsafedepth="+str(FINALITY_SAFE_DEPTH), "-cbhminage="+str(minAge)],
                ["-logtimemicros", "-debug=py", "-debug=mempool", "-debug=cbh", "-cbhsafedepth="+str(FINALITY_SAFE_DEPTH), "-cbhminage="+str(minAge)],
                ["-logtimemicros", "-debug=py", "-debug=mempool", "-debug=cbh", "-cbhsafedepth="+str(FINALITY_SAFE_DEPTH), "-cbhminage="+str(minAge)]
            ])

        if not split:
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:NUMB_OF_NODES])
            sync_mempools(self.nodes[1:NUMB_OF_NODES])

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def mark_logs(self, msg):
        print(msg)
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)
        self.nodes[2].dbg_log(msg)

    def run_test(self):
        ''' Test verifying the fix of some corner cases in the implementation of cbh '''
        TARGET_H  = 3
        FEE = Decimal('0.00005')

        self.mark_logs("Node1 generates %d blocks" % (CBH_DELTA_HEIGHT + TARGET_H))
        self.nodes[1].generate(CBH_DELTA_HEIGHT + TARGET_H)
        self.sync_all()

        # create a Tx having a '-1' as height and genesys blockhash in its scriptPubKey CHECKBLOCKATHEIGHT part
        payment_1 = Decimal('10.0')
        raw_tx_1 = create_tampered_rawtx_cbh(self.nodes[1], self.nodes[2], payment_1, FEE, MODE_HEIGHT)
         
        try:
            self.mark_logs("Node1 sending tx1 with tampered cbh script to Node2")
            tx_1 = self.nodes[1].sendrawtransaction(raw_tx_1['hex'])
            self.sync_all()
        except JSONRPCException as e:
            print(" ==> Tx has been rejected! {}".format(e.error['message']))
            # before rp fix fork this is expected to succeed
            assert_true(False)

        # create a Tx swapping height and blockhash in its scriptPubKey CHECKBLOCKATHEIGHT part
        payment_2 = Decimal('6.0')
        raw_tx_2 = create_tampered_rawtx_cbh(self.nodes[1], self.nodes[2], payment_2, FEE, MODE_SWAP_ARGS)
         
        try:
            self.mark_logs("Node1 sending tx2 with tampered cbh script to Node2")
            tx_2 = self.nodes[1].sendrawtransaction(raw_tx_2['hex'])
            self.sync_all()
        except JSONRPCException as e:
            print(" ==> Tx has been rejected! {}".format(e.error['message']))
            # before rp fix fork this is expected to succeed
            assert_true(False)

        # create a Tx with a non minimal encoded height
        payment_3 = Decimal('3.0')
        raw_tx_3 = create_tampered_rawtx_cbh(self.nodes[1], self.nodes[2], payment_3, FEE, MODE_NON_MIN_ENC)
         
        try:
            self.mark_logs("Node1 sending tx3 with tampered cbh script to Node2")
            tx_3 = self.nodes[1].sendrawtransaction(raw_tx_3['hex'])
            self.sync_all()
        except JSONRPCException as e:
            print(" ==> Tx has been rejected! {}".format(e.error['message']))
            # before rp fix fork this is expected to succeed
            assert_true(False)

        self.mark_logs("Node1 generates 1 block")
        bl = self.nodes[1].generate(1)[-1]
        self.sync_all()

        # check Node2 has relevant utxos
        usp = self.nodes[2].listunspent()
        txids = []
        for x in usp:
            txids.append(x['txid'])
        assert_true(tx_1 in txids)
        assert_true(tx_2 in txids)
        assert_true(tx_3 in txids)

        # check that in Node2 both cached and uncached balance has the same value
        node2_wrong_bal = payment_1 + payment_2 + payment_3
        assert_equal(self.nodes[2].getbalance(), node2_wrong_bal)
        assert_equal(Decimal(self.nodes[2].z_gettotalbalance()['total']), node2_wrong_bal)

        self.mark_logs("Node2 balance = {} in {} utxos".format(node2_wrong_bal, len(usp)))

        # trigger the issue using the tampered tx just received
        payment = Decimal('18.0')

        try:
            self.mark_logs("Node2 tries sending {} coins to Node0 using tampered utxos".format(payment))
            tx_bad = self.nodes[2].sendtoaddress(self.nodes[0].getnewaddress(), payment)
            # after rp fix fork this is expected to fail
            assert_true(False)
        except JSONRPCException as e:
            self.mark_logs("==> Node2 tx has not been accepted in mempool! {}".format(e.error['message']))
            assert_true("The transaction was rejected" in e.error['message'])
            self.sync_all()

        #check Node2 wallet has the expected number of txes, 3recvd/1sent
        assert_equal(self.nodes[2].getwalletinfo()['txcount'], 4)

        # reaching rp fix fork
        self.mark_logs("Node1 generates 100 blocks crossing rp fix fork")
        self.nodes[1].generate(100)
        self.sync_all()

        # check Node2 has no valid utxos
        usp = self.nodes[2].listunspent()
        assert_equal(len(usp), 0)
        self.mark_logs("Node2 now has no spendable utxos")

        # check Node2 wallet info still has tampered txes with a wrong cached balance
        assert_equal(self.nodes[2].getwalletinfo()['txcount'], 4)
        assert_equal(self.nodes[2].getbalance(), node2_wrong_bal)
        # but z_balance is now correct
        assert_equal(Decimal(self.nodes[2].z_gettotalbalance()['total']), Decimal("0.0"))

        # try to use send tampered txes after the rp fix fork has been reached
        payment = Decimal('3.0')
        raw_tx = create_tampered_rawtx_cbh(self.nodes[1], self.nodes[2], payment, FEE, MODE_HEIGHT)

        try:
            self.mark_logs("Node1 sending tx with tampered cbh script to Node2")
            tx = self.nodes[1].sendrawtransaction(raw_tx['hex'])
            # after rp fix fork this is expected to fail
            assert_true(False)
        except JSONRPCException as e:
            print(" ==> Tx has been rejected!")
            assert_true("scriptpubkey" in e.error['message'])

        payment = Decimal('2.0')
        raw_tx = create_tampered_rawtx_cbh(self.nodes[1], self.nodes[2], payment, FEE, MODE_SWAP_ARGS)
         
        try:
            self.mark_logs("Node1 sending tx with tampered cbh script to Node2")
            tx = self.nodes[1].sendrawtransaction(raw_tx['hex'])
            # after rp fix fork this is expected to fail
            assert_true(False)
        except JSONRPCException as e:
            print(" ==> Tx has been rejected!")
            assert_true("scriptpubkey" in e.error['message'])

        # create a Tx TODO
        raw_tx = create_tampered_rawtx_cbh(self.nodes[1], self.nodes[2], payment, FEE, MODE_NON_MIN_ENC)
         
        try:
            self.mark_logs("Node1 sending tx with tampered cbh script to Node2")
            tx = self.nodes[1].sendrawtransaction(raw_tx['hex'])
            # after rp fix fork this is expected to fail
            assert_true(False)
        except JSONRPCException as e:
            print(" ==> Tx has been rejected! {}".format(e.error['message']))
            assert_true("scriptpubkey" in e.error['message'])

        # check Node2 still has no valid utxos
        usp = self.nodes[2].listunspent()
        assert_equal(len(usp), 0)

        # restart Nodes 
        self.mark_logs("Stopping nodes")
        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False, 0)
        self.mark_logs("nodes restarted")

        # check Node2 still has no valid utxos
        usp = self.nodes[2].listunspent()
        assert_equal(len(usp), 0)

        # check Node2 wallet info still has tampered txes but now a correct cached balance
        assert_equal(self.nodes[2].getwalletinfo()['txcount'], 4)
        assert_equal(self.nodes[2].getbalance(), Decimal(0.0))
        assert_equal(Decimal(self.nodes[2].z_gettotalbalance()['total']), Decimal("0.0"))

if __name__ == '__main__':
    cbh_rpfix().main()
