#!/usr/bin/env python3

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_equal, assert_greater_than, initialize_chain_clean, \
    start_nodes, start_node, connect_nodes, stop_node, stop_nodes, \
    sync_blocks, sync_mempools, connect_nodes_bi, wait_bitcoinds, p2p_port, check_json_precision
from test_framework.util import swap_bytes
from decimal import Decimal
from test_framework.blocktools import create_tampered_rawtx_cbh, MODE_SWAP_ARGS
import os
import pprint

NUMB_OF_NODES = 3

# the scripts will not be checked if we have more than this depth of referenced block
FINALITY_SAFE_DEPTH = 500

# 0 means do not check any minimum age for referenced blocks in scripts
FINALITY_MIN_AGE = 75

# mainchain uses this value for targeting a blockhash in cbh script starting from chain tip backwards
CBH_DELTA_HEIGHT = 300

class cbh_doscpu(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass # Just open then close to create zero-length file

    def setup_network(self, split=False, minAge=FINALITY_MIN_AGE):
        self.nodes = []
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
            extra_args = [
                ["-allownonstandardtx=1", "-logtimemicros", "-debug=py", "-debug=net", "-debug=cbh", "-cbhsafedepth="+str(FINALITY_SAFE_DEPTH), "-cbhminage="+str(minAge)],
                ["-allownonstandardtx=1", "-logtimemicros", "-debug=py", "-debug=net", "-debug=cbh", "-cbhsafedepth="+str(FINALITY_SAFE_DEPTH), "-cbhminage="+str(minAge)],
                ["-allownonstandardtx=1", "-logtimemicros", "-debug=py", "-debug=net", "-debug=cbh", "-cbhsafedepth="+str(FINALITY_SAFE_DEPTH), "-cbhminage="+str(minAge)]
            ])

        if not split:
            connect_nodes_bi(self.nodes, 1, 2)
            connect_nodes_bi(self.nodes, 2, 1)
            sync_blocks(self.nodes[1:NUMB_OF_NODES])
            sync_mempools(self.nodes[1:NUMB_OF_NODES])

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 0)
        self.is_network_split = split
        self.sync_all()

    def mark_logs(self, msg):
        print(msg)
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)
        self.nodes[2].dbg_log(msg)

    def run_test(self):
        ''' This test creates a malformed transaction (Block hash and height swapped in checkblockatHeight).
            The transaction is accepted pre replayprotectionfixfork but its spending is prohibited both before
            and after the fork.
            The whole point of the test is to show that spending rejection happens for two different reasons.
        '''

        TARGET_H  = 3
        FEE = Decimal('0.00005')

        self.mark_logs("Node1 generates %d blocks" % (CBH_DELTA_HEIGHT + TARGET_H))
        self.nodes[1].generate(CBH_DELTA_HEIGHT + TARGET_H)
        self.sync_all()

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

        self.mark_logs("Node1 generates 1 block")
        bl = self.nodes[1].generate(1)[-1]
        self.sync_all()

        # check Node2 has relevant utxos
        usp = self.nodes[2].listunspent()
        txids = []
        for x in usp:
            txids.append(x['txid'])
        #assert_true(tx_1 in txids)
        assert_true(tx_2 in txids)

        # check that in Node2 both cached and uncached balance has the same value
        node2_wrong_bal = payment_2
        assert_equal(self.nodes[2].getbalance(), node2_wrong_bal)
        assert_equal(Decimal(self.nodes[2].z_gettotalbalance()['total']), node2_wrong_bal)

        self.mark_logs("Node2 balance = {} in {} utxos".format(node2_wrong_bal, len(usp)))

        # trigger the issue using the tampered tx just received
        payment = Decimal('5.0')
        fee     = Decimal('0.00005')

        amount  = Decimal('0')
        inputs  = []
        for x in usp:
            amount += Decimal(x['amount']) 
            inputs.append( {"txid":x['txid'], "vout":x['vout']})
            if amount >= payment+fee:
                break

        outputs = {
            self.nodes[2].getnewaddress(): (Decimal(amount) - payment - fee), # change
            self.nodes[0].getnewaddress(): payment }
        rawTx = self.nodes[2].createrawtransaction(inputs, outputs)
        signedRawTx = self.nodes[2].signrawtransaction(rawTx)

        try:
            self.mark_logs("Node2 tries sending {} coins to Node0 using tampered utxos BEFORE FORK".format(payment))
            self.nodes[2].sendrawtransaction(signedRawTx['hex'])
            # after rp fix fork this is expected to fail
            assert_true(False)
        except JSONRPCException as e:
            self.mark_logs("==> Node2 tx has not been accepted in mempool! {}".format(e.error['message']))
            # this has been rejected by the verifier thread
            assert_true("non-mandatory-script-verify-flag" in e.error['message'])
            self.sync_all()

        # reaching rp fix fork
        self.mark_logs("Node1 generates 100 blocks crossing rp fix fork")
        self.nodes[1].generate(100)
        self.sync_all()

        try:
            self.mark_logs("Node2 tries sending {} coins to Node0 using tampered utxos AFTER FORK".format(payment))
            self.nodes[2].sendrawtransaction(signedRawTx['hex'])
            # after rp fix fork this is expected to fail
            assert_true(False)
        except JSONRPCException as e:
            self.mark_logs("==> Node2 tx has not been accepted in mempool! {}".format(e.error['message']))
            # this has been rejected by the check input thread
            assert_true("bad-txns-output-scriptpubkey" in e.error['message'])
            self.sync_all()

if __name__ == '__main__':
    cbh_doscpu().main()
