#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, mark_logs, download_snapshot, \
    colorize as cc
from mempool_size_limit import load_from_storage, create_big_transaction
from test_framework.mc_test.mc_test import *
import os
import zipfile
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 1

NODE0_LIMIT_B = 5000000

NODE0_CERT_LIMIT_B = NODE0_LIMIT_B / 2

MAX_FEE = Decimal("999999")

NUM_CEASING_SIDECHAINS    = 2
NUM_NONCEASING_SIDECHAINS = 2

"""
This test is a follow up of mempool_size_limit.py and uses the same snapshot data
Please use that test to recreate the snapshot from scratch, as this file does not include
all the helper functions to that purpose.

Test replacement of transactions when certificate partition is full:
    - Clear the mempool
    - Add certificates up to MAX_SIZE / 2 (50%), fee_rate = F_low
    - Add transactions up to MAX_SIZE / 2 (50%), fee_rate = F_high
    - Add a new transaction, fee_rate = F_medium [REJECT]
    - Add a new transaction, fee_rate = F_highest [OK, one transaction gets evicted]

--------------
feerates ratings: F_high and F_medium are evaluated at realtime, given the feerates of txs array

F_highest ---   0.00020
           |
           |     \                      |- maxtxfeerate
F_high    ---    | --> midpoint between |             ~ 0.00000692
           |     /                      |- mintxfeerate
           |
           |     \                      |- mintxfeerate
F_medium  ---    | --> midpoint between |             ~ 0.00000347
           |     /                      |- F_low
           |
F_low     ---   ~2.245E-8

If creating the snapshot from scratch, verify that the evaluated values for F_high and F_medium
falls in these intervals!

"""

class mempool_size_limit_more(BitcoinTestFramework):
    def import_data_to_data_dir(self):
        # importing datadir resource
        # Tests checkpoint creation (during startup rescan) and usage, checks everything ok with old wallet
        snapshot_filename = 'mempool_size_limit_snapshot.zip'
        resource_file = download_snapshot(snapshot_filename, self.options.tmpdir)
        with zipfile.ZipFile(resource_file, 'r') as zip_ref:
            zip_ref.extractall(self.options.tmpdir)

    def setup_chain(self, split=False):
        self.import_data_to_data_dir()
        os.remove(self.options.tmpdir+'/node0/regtest/debug.log') # make sure that we have only logs from this test
        os.remove(self.options.tmpdir+'/node1/regtest/debug.log')
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args =
            [
                ['-debug=cert', '-debug=mempool', '-maxorphantx=10000', f'-maxmempool={NODE0_LIMIT_B / 1000000}', '-minconf=0', '-allownonstandardtx', '-maxtipage=3153600000'],
            ])

        sync_blocks(self.nodes)

    def setup_test(self):
        txs   = load_from_storage(self.options.tmpdir + "/txs/")
        ctxs  = load_from_storage(self.options.tmpdir + "/ctxs_r/")
        certs = []
        for s in range(NUM_CEASING_SIDECHAINS):
            certs.append(load_from_storage(self.options.tmpdir + f"/certs/{s}/"))
        for s in range(NUM_NONCEASING_SIDECHAINS):
            certs.append(load_from_storage(self.options.tmpdir + f"/certs/{s+NUM_CEASING_SIDECHAINS}/"))

        return txs, ctxs, certs


    def run_test(self):
        mark_logs("Loading snapshot...", self.nodes, DEBUG_MODE, color = 'e')
        txs, _, certs = self.setup_test() # We only need txs and certs

        txs.sort(key=lambda x: x['feerate'])
        for c in certs:
            c.sort(key=lambda x: x['quality'])

        mark_logs("Start from a clean mempool on node 0", self.nodes, DEBUG_MODE, color = 'e')
        self.nodes[0].clearmempool()

        mark_logs("Filling cert partition with certificates with fee = 0.00015 => feerate = F_low (~2.2465E-8)", self.nodes, DEBUG_MODE, color = 'c')
        mark_logs("Trying to be conservative and not overcome cert partition limit ", self.nodes, DEBUG_MODE, color = 'c')
        cert_fees = []
        cert_sent_size, last_cert_size = 0, 0
        maxfeerate, minfeerate = 0, MAX_FEE
        while (cert_sent_size < NODE0_CERT_LIMIT_B - last_cert_size):
            assert(len(certs[0]) > 0)
            c = certs[0].pop(0)
            cert_hex = self.nodes[0].sendrawtransaction(c['hex'])
            last_cert_size = len(c['hex']) // 2
            cert_fees.append(c['feerate'])
            cert_sent_size += last_cert_size
            minfeerate = min(minfeerate, c['feerate'])
            maxfeerate = max(maxfeerate, c['feerate'])

        mark_logs(cc('g', "certificates -- minfeerate: ") + f"{minfeerate:.8E}" + cc('g', " - maxfeerate: ") + f"{maxfeerate:.8E}", self.nodes, DEBUG_MODE)
        mempoolstatus = self.nodes[0].getmempoolinfo()
        mark_logs(cc('g', "Current mempool state: bytes: ") + f"{mempoolstatus['bytes']}" +
                  cc('g', " - bytes-for-tx: ") + f"{mempoolstatus['bytes-for-tx']}" +
                  cc('g', " - bytes-for-cert: ") + f"{mempoolstatus['bytes-for-cert']}", self.nodes, DEBUG_MODE)
        certPartitionStatus = int(mempoolstatus['bytes-for-cert'])
        print()


        mark_logs("Filling tx partition with transactions having feerate >= 0.00000692 (F_high)", self.nodes, DEBUG_MODE, color = 'c')
        mark_logs("Trying to be conservative and not overcome tx partition limit ", self.nodes, DEBUG_MODE, color = 'c')
        self.utxos = self.nodes[0].listunspent()
        tx_sent = 0
        last_tx_size = 0
        mintxfeerate, maxtxfeerate = MAX_FEE, 0
        discarded_txs = 0
        while True:
            if len(txs) > 0:
                tx = txs.pop(0)
            else:
                assert(len(self.utxos) > 0)
                tx = create_big_transaction(self.nodes[0], None, self.utxos.pop(), 512 * 128, (512 * 128) * Decimal("0.000008"), 0)
            if tx['feerate'] < Decimal('0.00000692'):
                discarded_txs += 1
                continue
            tx_hex = tx['hex']
            feerate = tx['feerate']
            tx_size = len(tx_hex)//2
            usage = self.nodes[0].getmempoolinfo()
            if int(usage['bytes-for-tx']) + tx_size > NODE0_LIMIT_B // 2 or int(usage['bytes']) + tx_size > NODE0_LIMIT_B:
                break
            self.nodes[0].sendrawtransaction(tx_hex, True)
            mintxfeerate, maxtxfeerate = min(mintxfeerate, feerate), max(maxtxfeerate, feerate)
            tx_sent += last_tx_size

        mark_logs(cc('g', "Rejected transactions (fee too low for this test): ") + f"{discarded_txs}", self.nodes, DEBUG_MODE)
        mark_logs(cc('g', "transactions -- mintxfeerate: ") + f"{mintxfeerate:1.8f}" + cc('g', " - maxtxfeerate: ") + f"{maxtxfeerate:1.8f}", self.nodes, DEBUG_MODE)
        mempoolstatus = self.nodes[0].getmempoolinfo()
        mark_logs(cc('g', "Current mempool state: bytes: ") + f"{mempoolstatus['bytes']}" +
                  cc('g', " - bytes-for-tx: ") + f"{mempoolstatus['bytes-for-tx']}" +
                  cc('g', " - bytes-for-cert: ") + f"{mempoolstatus['bytes-for-cert']}", self.nodes, DEBUG_MODE)
        # Assert that we did not evict any certificate
        assert_equal(mempoolstatus['bytes-for-cert'], certPartitionStatus)
        print()


        # We choose a feerate that still classify as high: the midpoint between the lower and higher tx fee of the previous step
        selected_high_feerate = mintxfeerate + (maxtxfeerate - mintxfeerate) / 2
        mark_logs(f"Filling tx partition with tiny transactions having feerate {selected_high_feerate:1.8f} (F_high)", self.nodes, DEBUG_MODE, color = 'c')
        mark_logs("It's ok to fill tx partition over its limit, we only do not want to evict certificates", self.nodes, DEBUG_MODE, color = 'c')
        last_tx_size = 0
        usage = self.nodes[0].getmempoolinfo()
        addr = self.nodes[0].getnewaddress()
        utxos = self.nodes[0].listunspent()
        while(int(usage['bytes']) < NODE0_LIMIT_B - last_tx_size):
            t = utxos.pop(0)
            inputs = [{"txid" : t["txid"], "vout" : t["vout"]}]

            outputs = {addr: t["amount"]}                                           #
            rawtx = self.nodes[0].createrawtransaction(inputs, outputs)             # Initial
            signedtx = self.nodes[0].signrawtransaction(rawtx, None, None, "NONE")  # estimation
            tx_size = len(signedtx['hex']) // 2                                     #

            outputs = {addr: t["amount"] - Decimal(selected_high_feerate * tx_size)}
            rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
            signedtx = self.nodes[0].signrawtransaction(rawtx, None, None, "NONE")
            try:
                self.nodes[0].sendrawtransaction(signedtx['hex'], True)
            except JSONRPCException as e:
                errorString = e.error['message']
                print(errorString)
                assert(False)
            new_usage = self.nodes[0].getmempoolinfo()
            last_tx_size = int(new_usage['bytes-for-tx']) - int(usage['bytes-for-tx'])
            usage = new_usage
        mempoolstatus = self.nodes[0].getmempoolinfo()
        mark_logs(cc('g', "Current mempool state: bytes: ") + f"{mempoolstatus['bytes']}" +
                  cc('g', " - bytes-for-tx: ") + f"{mempoolstatus['bytes-for-tx']}" +
                  cc('g', " - bytes-for-cert: ") + f"{mempoolstatus['bytes-for-cert']}", self.nodes, DEBUG_MODE)
        # Assert that we did not evict any certificate
        assert_equal(mempoolstatus['bytes-for-cert'], certPartitionStatus)
        print()


        selected_medium_feerate = float(maxfeerate) + (float(mintxfeerate) - float(maxfeerate)) / 2
        mark_logs(f"Sending a medium feerate transaction (F_medium = {selected_medium_feerate:1.8f}) will result in a error", self.nodes, DEBUG_MODE, color = 'c')
        t = utxos.pop(0)
        inputs = [{"txid" : t["txid"], "vout" : t["vout"]}]

        outputs = {addr: t["amount"]}                                           #
        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)             # Initial
        signedtx = self.nodes[0].signrawtransaction(rawtx, None, None, "NONE")  # estimation
        tx_size = len(signedtx['hex']) // 2                                     #

        outputs = {addr: t["amount"] - Decimal(str(selected_medium_feerate * tx_size))}
        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
        signedtx = self.nodes[0].signrawtransaction(rawtx, None, None, "NONE")
        try:
            self.nodes[0].sendrawtransaction(signedtx['hex'], True)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            print(cc('y', errorString))
        mempoolstatus = self.nodes[0].getmempoolinfo()
        mark_logs(cc('g', "Current mempool state: bytes: ") + f"{mempoolstatus['bytes']}" +
                  cc('g', " - bytes-for-tx: ") + f"{mempoolstatus['bytes-for-tx']}" +
                  cc('g', " - bytes-for-cert: ") + f"{mempoolstatus['bytes-for-cert']}", self.nodes, DEBUG_MODE)
        # Assert that we did not evict any certificate
        assert_equal(mempoolstatus['bytes-for-cert'], certPartitionStatus)
        print()


        mark_logs("Sending a highest feerate transaction (F_highest = 0.0002) will result in the eviction of a low fee transaction", self.nodes, DEBUG_MODE, color = 'c')
        t = utxos.pop(0)
        inputs = [{"txid" : t["txid"], "vout" : t["vout"]}]

        outputs = {addr: t["amount"]}                                           #
        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)             # Initial
        signedtx = self.nodes[0].signrawtransaction(rawtx, None, None, "NONE")  # estimation
        tx_size = len(signedtx['hex']) // 2                                     #

        outputs = {addr: t["amount"] - Decimal(str(0.0002 * tx_size))}
        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
        signedtx = self.nodes[0].signrawtransaction(rawtx, None, None, "NONE")
        try:
            self.nodes[0].sendrawtransaction(signedtx['hex'], True)
        except JSONRPCException as e:
            errorString = e.error['message']
            print(errorString)
            assert(False)
        mempoolstatus = self.nodes[0].getmempoolinfo()
        mark_logs(cc('g', "Current mempool state: bytes: ") + f"{mempoolstatus['bytes']}" +
                  cc('g', " - bytes-for-tx: ") + f"{mempoolstatus['bytes-for-tx']}" +
                  cc('g', " - bytes-for-cert: ") + f"{mempoolstatus['bytes-for-cert']}", self.nodes, DEBUG_MODE)
        # Assert that we did not evict any certificate
        assert_equal(mempoolstatus['bytes-for-cert'], certPartitionStatus)

if __name__ == '__main__':
    mempool_size_limit_more().main()
