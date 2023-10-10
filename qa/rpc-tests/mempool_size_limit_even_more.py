#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.authproxy import JSONRPCException
from test_framework.util import initialize_chain_clean, start_nodes, mark_logs,\
    get_epoch_data, swap_bytes, assert_greater_than, download_snapshot, colorize as cc
from mempool_size_limit import load_from_storage, create_big_transactions, create_sidechains, \
    write_to_file
from test_framework.mc_test.mc_test import *
import os
import zipfile
from decimal import Decimal

DEBUG_MODE = 1
NUMB_OF_NODES = 1
EPOCH_LENGTH = 100

class feeAmounts():
    HIGHEST = Decimal('0.0001')
    HIGH    = Decimal('0.00005')
    MEDIUM  = Decimal('0.00001')
    LOW     = Decimal('0.000005')
    LOWEST  = Decimal('0.000001')
    NULL    = Decimal('0')

FT_SC_FEE      = feeAmounts.NULL
MBTR_SC_FEE    = feeAmounts.NULL
CERT_FEE       = Decimal('0.00015')

NODE0_LIMIT_B = 4000000
NODE1_LIMIT_B = 4000000

NODE0_CERT_LIMIT_B = NODE0_LIMIT_B / 2
NODE1_CERT_LIMIT_B = NODE1_LIMIT_B / 2

EPSILON = 100000
MAX_FEE = Decimal("999999")

NUM_CEASING_SIDECHAINS    = 750

PARAMS_NAME = "sc"
USE_SNAPSHOT = True

"""
This test is a follow up of mempool_size_limit.py and mempool_size_limit_more.py, and uses a new snapshot data.
Snapshot creation takes slightly more than an hour, and the resulting archive will be about 2.5 GBytes in size.

Test MAX_SIZE for certificates
    Clear the mempool
    Add certificates up to the max size, all related to different sidechains (no duplications), fee_rate = F_medium [OK]
        Add one more certificate exceeding the max size, fee_rate = F_low [REJECT]
        Add one more certificate exceeding the max size, fee_rate = F_high [ACCEPT, one certificate gets evicted]
    Add transactions up to MAX_SIZE / 2, fee_rate = F_highest [ACCEPT, certificates get evicted]
        Add one more transaction, fee_rate = F_highest [REJECTED]
"""

class mempool_size_limit_even_more(BitcoinTestFramework):

    def import_data_to_data_dir(self):
        # importing datadir resource
        # Tests checkpoint creation (during startup rescan) and usage, checks everything ok with old wallet
        snapshot_filename = 'mempool_size_limit_even_more_NP_snapshot.zip'
        resource_file = download_snapshot(snapshot_filename, self.options.tmpdir)
        with zipfile.ZipFile(resource_file, 'r') as zip_ref:
            zip_ref.extractall(self.options.tmpdir)

    def setup_chain(self, split=False):
        if (USE_SNAPSHOT):
            self.import_data_to_data_dir()
            os.remove(self.options.tmpdir+'/node0/regtest/debug.log') # make sure that we have only logs from this test
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args =
            [
                ['-debug=cert', '-debug=mempool', '-maxorphantx=10000', f'-maxmempool={NODE0_LIMIT_B / 1000000}', '-minconf=0', '-allownonstandardtx'],
            ])

        self.is_network_split = False
        self.sync_all()

    def create_sc_certificates(self, mcTest, sidechain, scidx, epoch_number, quality, fee, size, raw_bwt_outs = None):
        os.makedirs(self.options.tmpdir + "/certs", exist_ok=True)
        os.makedirs(self.options.tmpdir + f"/certs/{scidx}", exist_ok=True)
        if raw_bwt_outs is None:
            raw_bwt_outs = []
            for _ in range(128):
                raw_bwt_outs.append({"address":self.nodes[0].getnewaddress(), "amount":Decimal("0.0001")})
        i = 0
        if (self.tot_sc_cert_size < size and len(self.utxos) > 0):
            t = self.utxos.pop()
            raw_inputs = [{'txid' : t['txid'], 'vout' : t['vout']}]
            amt = t["amount"] - fee
            raw_outs = {self.nodes[0].getnewaddress(): amt}

            scid = sidechain["id"]
            epoch_len = sidechain["epoch_len"]
            par_name = sidechain["params"]
            constant = sidechain["constant"]
            ref_height = self.nodes[0].getblockcount()
            ceasing = True if epoch_len > 0 else False

            scid_swapped = str(swap_bytes(scid))
            _, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[0], epoch_len, is_non_ceasing = not ceasing, reference_height = ref_height)

            proof = mcTest.create_test_proof(par_name,
                                             scid_swapped,
                                             epoch_number,
                                             quality,
                                             MBTR_SC_FEE,
                                             FT_SC_FEE,
                                             epoch_cum_tree_hash,
                                             prev_cert_hash,
                                             constant = constant,
                                             pks      = [raw_bwt_outs[i]["address"] for i in range(128)],
                                             amounts  = [raw_bwt_outs[i]["amount"] for i in range(128)])

            raw_params = {
                "scid": scid,
                "quality": quality,
                "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash,
                "scProof": proof,
                "withdrawalEpochNumber": epoch_number
            }

            raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
            signed_cert = self.nodes[0].signrawtransaction(raw_cert)
            csize = len(signed_cert['hex']) // 2
            feerate = fee / csize
            self.tot_sc_cert_size += csize
            write_to_file(self.options.tmpdir + f"/certs/{scidx}/c_{quality}", feerate, signed_cert["hex"])
            cert = {'quality': quality, 'feerate': feerate, 'hex': signed_cert["hex"]}
            print("Size of total cert produced: " + str(self.tot_sc_cert_size))

            return cert
        return None
    
    def create_certificate(self, scidx, epoch_number, quality, fee, raw_bwt_outs = None):
        if raw_bwt_outs is None:
            raw_bwt_outs = []
            for _ in range(128):
                raw_bwt_outs.append({"address":self.nodes[0].getnewaddress(), "amount":Decimal("0.0001")})

        utxos = self.nodes[0].listunspent(0)
        t = utxos.pop()
        raw_inputs = [{'txid' : t['txid'], 'vout' : t['vout']}]
        amt = t["amount"] - fee
        raw_outs = {self.nodes[0].getnewaddress(): amt}

        sc = self.nodes[0].getscinfo("*")['items'][scidx]
        scid = sc["scid"]
        epoch_len = sc["withdrawalEpochLength"]
        ceasing = True if epoch_len > 0 else False
        par_name = par_name = PARAMS_NAME + ("c" if ceasing else "nc") + str(scidx)
        constant = sc["constant"]
        ref_height = self.nodes[0].getblockcount()

        scid_swapped = str(swap_bytes(scid))
        _, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[0], epoch_len, is_non_ceasing = not ceasing, reference_height = ref_height)

        proof = self.mcTest.create_test_proof(par_name,
                                            scid_swapped,
                                            epoch_number,
                                            quality,
                                            MBTR_SC_FEE,
                                            FT_SC_FEE,
                                            epoch_cum_tree_hash,
                                            prev_cert_hash,
                                            constant = constant,
                                            pks      = [raw_bwt_outs[i]["address"] for i in range(128)],
                                            amounts  = [raw_bwt_outs[i]["amount"] for i in range(128)])

        raw_params = {
            "scid": scid,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash,
            "scProof": proof,
            "withdrawalEpochNumber": epoch_number
        }
        raw_cert    = self.nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
        signed_cert = self.nodes[0].signrawtransaction(raw_cert)
        csize = len(signed_cert['hex'])//2
        cert = {'quality': quality, 'feerate': fee / csize, 'hex': signed_cert["hex"]}

        return cert, t


    def setup_test(self):
        if USE_SNAPSHOT:
            txs   = load_from_storage(self.options.tmpdir + "/txs/")
            certs = []
            for s in range(NUM_CEASING_SIDECHAINS):
                certs.append(load_from_storage(self.options.tmpdir + f"/certs/{s}/"))

        else:
            mark_logs("Node 0 generates {} block".format(ForkHeights['NON_CEASING_SC']), self.nodes, DEBUG_MODE)
            self.nodes[0].generate(ForkHeights['NON_CEASING_SC'] + 800)
            self.sync_all()

            # Create sidechains. Enjoy the wait.
            mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
            sc = []
            sc.extend(create_sidechains(self.nodes, mcTest, NUM_CEASING_SIDECHAINS, ceasing = True,
                                        epoch_len = EPOCH_LENGTH, amount = Decimal('3')))
            self.nodes[0].generate(EPOCH_LENGTH)
            self.sync_all()

            self.utxos = self.nodes[0].listunspent(0)
            txs = create_big_transactions(self.nodes[0], self.options.tmpdir, self.utxos, NODE0_LIMIT_B + EPSILON)

            # Create certificates, one per sc
            certs = list()
            self.tot_sc_cert_size = 0
            raw_bwt_outs = []
            for _ in range(128):
                raw_bwt_outs.append({"address":self.nodes[0].getnewaddress(), "amount":Decimal("0.0001")})
            for i in range(NUM_CEASING_SIDECHAINS):
                cert = self.create_sc_certificates(mcTest, sc[i], i, 0, 0, feeAmounts.MEDIUM, EPSILON + NODE0_LIMIT_B, raw_bwt_outs)
                if cert is not None:
                    certs.append(cert)

        return txs, certs


    def run_test(self):
        mark_logs("Loading or generating the snapshot...", self.nodes, DEBUG_MODE, color = 'e')
        txs, certs = self.setup_test()

        txs.sort(key=lambda x: x['feerate'])
        if USE_SNAPSHOT:
            certs = [x[0] for x in certs if len(x) > 0] # purge empty elements

        mark_logs("Filling the entire mempool with certificates having fee = 0.00001 => feerate = F_medium ~ 1.4974e-9", self.nodes, DEBUG_MODE, color = 'c')
        cert_sent_size, usage, last_cert_size = 0, 0, 0
        initial_cert_n = len(certs)
        maxfeerate, minfeerate = 0, MAX_FEE
        while (cert_sent_size < NODE0_LIMIT_B and usage < NODE0_LIMIT_B - last_cert_size):
            assert(len(certs) > 0)
            c = certs.pop(0)
            try:
                self.nodes[0].sendrawtransaction(c['hex'])
            except JSONRPCException as e:
                errorString = e.error['message']
                print("Send certificate failed with reason {}".format(errorString))
                assert(False)
            last_cert_size = len(c['hex']) // 2
            cert_sent_size += last_cert_size
            usage = int(self.nodes[0].getmempoolinfo()['bytes'])
            minfeerate, maxfeerate = min(minfeerate, c['feerate']), max(maxfeerate, c['feerate'])
        mark_logs(cc('g', "certificates sent to mempool: ") + f"{initial_cert_n - len(certs)}", self.nodes, DEBUG_MODE)
        mark_logs(cc('g', "certificates left: ") + f"{len(certs)}", self.nodes, DEBUG_MODE)
        mark_logs(cc('g', "certificates -- minfeerate: ") + f"{minfeerate:.8E}" + cc('g', " - maxfeerate: ") + f"{maxfeerate:.8E}", self.nodes, DEBUG_MODE)
        mempoolstatus = self.nodes[0].getmempoolinfo()
        mark_logs(cc('g', "Current mempool state: bytes: ") + f"{mempoolstatus['bytes']}" +
                  cc('g', " - bytes-for-tx: ") + f"{mempoolstatus['bytes-for-tx']}" +
                  cc('g', " - bytes-for-cert: ") + f"{mempoolstatus['bytes-for-cert']}", self.nodes, DEBUG_MODE)
        cert_partition = int(mempoolstatus['bytes-for-cert'])
        print()


        mark_logs("Adding one more certificate with feerate = F_medium exceeding the max size will fail", self.nodes, DEBUG_MODE, color = 'c')
        c = certs.pop(0)
        try:
            mark_logs(cc('e', "Cert fees: ") + f"{c['feerate']:.8E}", self.nodes, DEBUG_MODE)
            self.nodes[0].sendrawtransaction(c['hex'])
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            print(cc('y', f"Send certificate failed with reason {errorString}"))
        print()


        mark_logs("Adding one more certificate with feerate = F_high ~ 7.4884e-9 exceeding the max size will be ok and one F_medium cert gets evicted", self.nodes, DEBUG_MODE, color = 'c')
        raw_bwt_outs = []
        for _ in range(128):
            raw_bwt_outs.append({"address":self.nodes[0].getnewaddress(), "amount":Decimal("0.0001")})
        self.mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        cert, tx_used_as_input = self.create_certificate(NUM_CEASING_SIDECHAINS - 1, 0, 0, feeAmounts.HIGH, raw_bwt_outs)
        try:
            mark_logs(cc('e', "Cert fees: ") + f"{cert['feerate']:.8E}", self.nodes, DEBUG_MODE)
            self.nodes[0].sendrawtransaction(cert['hex'])
        except JSONRPCException as e:
            errorString = e.error['message']
            print("Send certificate failed with reason {}".format(errorString))
            assert(False)
        mempoolstatus = self.nodes[0].getmempoolinfo()
        mark_logs(cc('g', "Current mempool state: bytes: ") + f"{mempoolstatus['bytes']}" +
                  cc('g', " - bytes-for-tx: ") + f"{mempoolstatus['bytes-for-tx']}" +
                  cc('g', " - bytes-for-cert: ") + f"{mempoolstatus['bytes-for-cert']}", self.nodes, DEBUG_MODE)
        print()


        mark_logs("Add transactions with feerate = F_highest up to MAX_SIZE/2: certificates get evicted", self.nodes, DEBUG_MODE, color = 'c')
        tx_sent_size, usage, last_tx_size = 0, 0, 0
        mintxfeerate, maxtxfeerate = MAX_FEE, 0
        usage = int(self.nodes[0].getmempoolinfo()['bytes-for-cert'])
        while (usage > NODE0_LIMIT_B // 2):
            assert(len(txs) > 0)
            tx = txs.pop()
            tx_hex = tx['hex']
            feerate = tx['feerate']
            temp_tx = self.nodes[0].decoderawtransaction(tx_hex)        # Prevent double spending error by ignoring
            if temp_tx['vin'][0]['txid'] == tx_used_as_input['txid']:   # this tx if its input has already been used as
                continue                                                # input for the certificate we sent previously
            self.nodes[0].sendrawtransaction(tx_hex, True)
            last_tx_size = len(tx_hex) // 2
            tx_sent_size += last_tx_size
            mintxfeerate, maxtxfeerate = min(mintxfeerate, feerate), max(maxtxfeerate, feerate)
            usage = int(self.nodes[0].getmempoolinfo()['bytes-for-cert'])
        mempoolstatus = self.nodes[0].getmempoolinfo()
        mark_logs(cc('g', "transactions -- minfeerate: ") + f"{mintxfeerate:1.8f}" + cc('g', " - maxfeerate: ") + f"{maxtxfeerate:1.8f}", self.nodes, DEBUG_MODE)
        mark_logs(cc('g', "Current mempool state: bytes: ") + f"{mempoolstatus['bytes']}" +
                  cc('g', " - bytes-for-tx: ") + f"{mempoolstatus['bytes-for-tx']}" +
                  cc('g', " - bytes-for-cert: ") + f"{mempoolstatus['bytes-for-cert']}", self.nodes, DEBUG_MODE)
        # Assert that certificates got evicted
        assert_greater_than(cert_partition, int(mempoolstatus['bytes-for-cert']))
        print()


        mark_logs("Add another transactions with feerate = F_highest > 0.0001 will fail", self.nodes, DEBUG_MODE, color = 'c')
        tx = txs.pop()
        tx_hex = tx['hex']
        try:
            mark_logs(cc('e', "Tx fees: ") + f"{tx['feerate']:1.8f}", self.nodes, DEBUG_MODE)
            self.nodes[0].sendrawtransaction(tx_hex, True)
            assert(False)
        except JSONRPCException as e:
            errorString = e.error['message']
            print(cc('y', errorString))
        mark_logs(cc('g', "Current mempool state: bytes: ") + f"{mempoolstatus['bytes']}" +
                  cc('g', " - bytes-for-tx: ") + f"{mempoolstatus['bytes-for-tx']}" +
                  cc('g', " - bytes-for-cert: ") + f"{mempoolstatus['bytes-for-cert']}", self.nodes, DEBUG_MODE)


if __name__ == '__main__':
    mempool_size_limit_even_more().main()