#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs,\
    get_epoch_data, hex_str_to_bytes, swap_bytes, download_snapshot
from test_framework.mc_test.mc_test import *
import os
import zipfile
import time
from decimal import Decimal, ROUND_DOWN, ROUND_05UP
from getblocktemplate_proposals import varlenEncode, b2x

USE_SNAPSHOT = True # set to False to regenerate test data.

DEBUG_MODE = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH = 10

FT_SC_FEE      = Decimal('0')
MBTR_SC_FEE    = Decimal('0')
CERT_FEE       = Decimal('0.00015')
PARAMS_NAME = "sc"

NODE0_LIMIT_B = 5000000
NODE1_LIMIT_B = 4000000

NODE0_CERT_LIMIT_B = NODE0_LIMIT_B / 2
NODE1_CERT_LIMIT_B = NODE1_LIMIT_B / 2

EPSILON = 100000
MAX_FEE = Decimal("999999")

NUM_CEASING_SIDECHAINS    = 2
NUM_NONCEASING_SIDECHAINS = 2

NUM_OUT = 128
BIG_SCRIPT_OVERHEAD_B = 6685

def write_to_file(filePath, feerate, hex):
    file = open(filePath, "wb")
    file.write(bytes(str(feerate) + '\n', encoding='utf8'))
    file.write(bytes(hex, encoding='utf8'))
    file.close()

def load_from_file(rFile):
    feerate = Decimal(rFile.readline().decode("utf-8"))
    hex     = rFile.readline().decode("utf-8")
    return {"feerate": feerate, "hex": hex}

def load_from_storage(path):
    res = []
    certs = "certs" in path
    for rf in os.listdir(path):
        rFile = open(path + rf, "rb")
        entry = load_from_file(rFile)
        if (certs):
            assert(rf.find("c_") == 0)
            entry["quality"] = int(rf[2:])
        res.append(entry)
        rFile.close()
    return res

def satoshi_round(amount):
    return Decimal(amount).quantize(Decimal('0.00000001'), rounding=ROUND_DOWN)

def amount_to_satoshi(amount):
    return int(Decimal(amount * 100000000).quantize(Decimal('1'), rounding=ROUND_05UP))

def create_tx_script(script_size, amount):
    bytes_per_out = script_size // NUM_OUT
    script_pubkey = "6a4d" # OP_RETURN OP_PUSH2
    script_pubkey += b2x(bytes_per_out.to_bytes(2, 'little')) # script_size bytes
    for i in range(bytes_per_out):
        script_pubkey += "01"

    # Push a fake hash and height on the stack, followed by checkblockatheight
    script_pubkey += "4c20"  # PUSH1 32bytes
    script_pubkey += "0000000000000000000000000000000000000000000000000000000000000000" # 0
    script_pubkey += "51"  # PUSH 1
    script_pubkey += "b4" # OP_CHECKBLOCKATHEIGHT
    ## concatenate NUM_OUT txouts of above script_pubkey which we'll insert before the txout for change
    txouts = b2x(varlenEncode(NUM_OUT))
    satoshis_amount = amount_to_satoshi(amount) // NUM_OUT
    amount_str = b2x((satoshis_amount).to_bytes(8, 'little'))
    satoshis_amount_remainder = satoshis_amount + (amount_to_satoshi(amount) % NUM_OUT)
    amount_str_remainder = b2x((satoshis_amount_remainder).to_bytes(8, 'little'))
    for k in range(NUM_OUT):
        # add txout value
        txouts += amount_str if (k > 0) else amount_str_remainder
        # add length of script_pubkey
        txouts += b2x(varlenEncode(len(script_pubkey)//2))
        # add script_pubkey
        # txouts += b2x(ser_string(script_pubkey))
        txouts += script_pubkey
    return txouts

'''
TODO: replace the default ones with the _noraw variants of the functions below
#from test_framework.script import CScript, CScriptNum, OP_CHECKBLOCKATHEIGHT, OP_RETURN, OP_PUSHDATA1, OP_PUSHDATA2, OP_1
#from test_framework.mininode import CTransaction, ToHex, ser_string
#from io import BytesIO
#from binascii import unhexlify, hexlify
def create_tx_script_noraw(script_size, amount):
    bytes_per_out = script_size // NUM_OUT

    random_bytes = hex_str_to_bytes("01" * bytes_per_out)

    null_hash = hex_str_to_bytes("0000000000000000000000000000000000000000000000000000000000000000")
    script_pubkey = CScript([OP_RETURN, OP_PUSHDATA2, bytes_per_out, random_bytes, OP_PUSHDATA1, 32, null_hash, OP_1, OP_CHECKBLOCKATHEIGHT])
    print(script_pubkey)

    return script_pubkey

def create_big_transaction_noraw(node, tmpdir, input_t, script_size, fee, i):
    assert(fee < input_t['amount'])
    txscript = create_tx_script2(script_size, input_t['amount'] - fee)

    inputs = []
    inputs.append({ "txid" : input_t["txid"], "vout" : input_t["vout"], "scriptPubKey": input_t["scriptPubKey"]})
    outputs = {node.getnewaddress(): Decimal((input_t['amount'] - fee)/NUM_OUT) for i in range(NUM_OUT)}
    rawtx = node.createrawtransaction(inputs, outputs)

    # build an object from the raw Tx in order to be able to modify it
    tx_01 = CTransaction()
    f = BytesIO(unhexlify(rawtx))
    tx_01.deserialize(f)

    for vout_idx in range(len(tx_01.vout)):
        tx_01.vout[vout_idx].scriptPubKey = txscript

    tx_01.rehash()
    signresult = node.signrawtransaction(ToHex(tx_01), inputs, None, "NONE")
    tsize = len(signresult['hex']) // 2
    feerate = fee / tsize
    write_to_file(tmpdir + "/tx" + str(i), feerate, signresult["hex"])
    tx = {'feerate': feerate, 'hex': signresult["hex"]}
    return tx, tsize
'''

def create_big_transaction(node, tmpdir, input_t, script_size, fee, i):
    assert(fee < input_t['amount'])
    txscript = create_tx_script(script_size, input_t['amount'] - fee)

    inputs = []
    inputs.append({ "txid" : input_t["txid"], "vout" : input_t["vout"], "scriptPubKey": input_t["scriptPubKey"]})
    addr = node.getnewaddress()
    outputs = {addr: Decimal(0)} # This will be overwritten anyway
    rawtx = node.createrawtransaction(inputs, outputs)
    out_num_pos = rawtx.find('ffffffff')
    script_position = rawtx.find('76a914')
    script_length = int(rawtx[script_position - 2 : script_position], 16) * 2

    newtx = rawtx[0:out_num_pos + 8]
    newtx = newtx + txscript
    newtx = newtx + rawtx[script_position + script_length:]

    signresult = node.signrawtransaction(newtx, inputs, None, "NONE")
    tsize = len(signresult['hex']) // 2
    feerate = fee / tsize
    if (tmpdir is not None):
        write_to_file(tmpdir + "/tx" + str(i), feerate, signresult["hex"])
    tx = {'feerate': feerate, 'hex': signresult["hex"]}
    return tx, tsize


def create_big_transactions(node, tmpdir, utxos, size):
    txs = []
    tot_size = 0
    i = 0
    os.makedirs(tmpdir + "/txs")
    minfeerate, maxfeerate = MAX_FEE, Decimal("0")
    fee = Decimal("0.5") + Decimal("0.01") * i
    while (tot_size < size):
        tx, tsize = create_big_transaction(node, tmpdir + "/txs", utxos.pop(), 512 * NUM_OUT, fee, i)
        tot_size += tsize
        txs.append(tx)
        i += 1
        feerate = tx['feerate']
        minfeerate = min(minfeerate, feerate)
        maxfeerate = max(maxfeerate, feerate)
        fee += Decimal("0.0000001")

    print("Created big transaction for a total size: " + str(tot_size))
    print("Min feerate: " + str(minfeerate))
    print("Max feerate: " + str(maxfeerate))
    return txs, minfeerate, maxfeerate

def create_sc_certificates(nodes, mcTest, tmpdir, utxos, sidechain, scidx, epoch_number, quality, fee, size):
    certs = []
    tot_size = 0
    raw_bwt_outs = []
    os.makedirs(tmpdir + "/certs", exist_ok=True)
    os.makedirs(tmpdir + f"/certs/{scidx}", exist_ok=True)
    for i in range(NUM_OUT):
        raw_bwt_outs.append({"address": nodes[1].getnewaddress(), "amount":Decimal("0.0001")})
    i = 0
    while (tot_size < size and len(utxos) > 0):
        t = utxos.pop()
        raw_inputs = [{'txid' : t['txid'], 'vout' : t['vout']}]
        amt = t["amount"] - fee
        raw_outs = {nodes[0].getnewaddress(): amt}

        scid      = sidechain["id"]
        epoch_len = sidechain["epoch_len"]
        par_name  = sidechain["params"]
        constant  = sidechain["constant"]
        ref_height = nodes[0].getblockcount()
        ceasing = True if epoch_len > 0 else False

        scid_swapped = str(swap_bytes(scid))
        _, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, nodes[0], epoch_len, is_non_ceasing = not ceasing, reference_height = ref_height)

        proof = mcTest.create_test_proof(par_name,
                                         scid_swapped,
                                         epoch_number,
                                         quality,
                                         MBTR_SC_FEE,
                                         FT_SC_FEE,
                                         epoch_cum_tree_hash,
                                         prev_cert_hash,
                                         constant = constant,
                                         pks      = [raw_bwt_outs[i]["address"] for i in range(NUM_OUT)],
                                         amounts  = [raw_bwt_outs[i]["amount"] for i in range(NUM_OUT)])

        raw_params = {
            "scid": scid,
            "quality": quality,
            "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash,
            "scProof": proof,
            "withdrawalEpochNumber": epoch_number
        }

        raw_cert    = nodes[0].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
        signed_cert = nodes[0].signrawtransaction(raw_cert)
        csize = len(signed_cert['hex'])//2
        feerate = fee / csize
        tot_size += csize
        write_to_file(tmpdir + f"/certs/{scidx}/c_{quality}", feerate, signed_cert["hex"])
        certs.append({'quality': i, 'feerate': feerate, 'hex': signed_cert["hex"]})
        quality += 1
        print("Tot cert: " + str(tot_size))
        i += 1
        if (epoch_len == 0):
            return certs

    return certs

def create_chained_transactions(node, tmpdir, utxos, script_size, low_feerate, high_feerate):
    os.makedirs(tmpdir)
    addr = node.getnewaddress()
    txs = []

    EXP_SIZE = 228
    low_fee  = low_feerate * EXP_SIZE
    high_fee = high_feerate * (script_size + BIG_SCRIPT_OVERHEAD_B)
    amt = Decimal("0")
    while (amt < high_fee + low_fee):
        t = utxos.pop()
        amt = t['amount']

    inputs = []
    inputs.append({ "txid" : t["txid"], "vout" : t["vout"]})
    outputs = {}
    send_value = t['amount'] - low_fee
    outputs[addr] = satoshi_round(send_value)
    rawtx = node.createrawtransaction(inputs, outputs)
    signresult = node.signrawtransaction(rawtx, None, None, "NONE")
    csize = len(signresult['hex']) // 2
    feerate = low_fee / csize
    assert(abs(csize - EXP_SIZE) <= 1)

    write_to_file(tmpdir + "/tx0", feerate, signresult["hex"])
    txs.append({'feerate': feerate, 'hex': signresult["hex"]})

    ptx = node.decoderawtransaction(signresult["hex"])

    in_value = t['amount'] - low_fee
    tx, _ = create_big_transaction(node, tmpdir, { "txid" : ptx['txid'], "vout" : 0, "amount" : in_value, "scriptPubKey": ptx['vout'][0]['scriptPubKey']['hex']}, script_size, high_fee, 1)

    txs.append({'feerate': tx['feerate'], 'hex': tx['hex']})

    return txs

def create_chained_transactions_and_remover(node, tmpdir, utxos, low_feerate, high_feerate):
    ctxs_r = create_chained_transactions(node, tmpdir, utxos, 24 * NUM_OUT, low_feerate, low_feerate)

    # EXP_SIZE should be ~2.5x the size of one of the chained tx created above
    EXP_SIZE = 60 * NUM_OUT
    high_fee = high_feerate * (EXP_SIZE + BIG_SCRIPT_OVERHEAD_B)
    tx, _ = create_big_transaction(node, tmpdir, utxos.pop(), EXP_SIZE, high_fee, 2)

    ctxs_r.append({'feerate': tx['feerate'], 'hex': tx['hex']})

    return ctxs_r


def create_sidechains(nodes, mcTest, num, ceasing, epoch_len = EPOCH_LENGTH, amount = Decimal("15")):
    res = []
    # generate wCertVk and constant
    for i in range(num):
        newsc = {}
        constant = generate_random_field_element_hex()
        ep_len = epoch_len if ceasing else 0
        par_name = PARAMS_NAME + ("c" if ceasing else "nc") + str(i)

        vk = mcTest.generate_params(par_name, keyrot=True)

        # generate sidechain
        cmdInput = {
            "version": 2,
            "withdrawalEpochLength": ep_len,
            "toaddress": "dada",
            "amount": amount,
            "wCertVk": vk,
            "constant": constant,
        }

        ret = nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        mark_logs("Node 0 created the SC {} via tx {}.".format(scid, creating_tx), nodes, DEBUG_MODE)
        newsc["id"] = scid
        newsc["params"] = par_name
        newsc["constant"] = constant
        newsc["epoch_len"] = ep_len
        res.append(newsc)
    return res


class mempool_size_limit(BitcoinTestFramework):
    def import_data_to_data_dir(self):
        # importing datadir resource
        # Tests checkpoint creation (during startup rescan) and usage, checks everything ok with old wallet
        snapshot_filename = 'mempool_size_limit_snapshot.zip'
        resource_file = download_snapshot(snapshot_filename, self.options.tmpdir)
        with zipfile.ZipFile(resource_file, 'r') as zip_ref:
            zip_ref.extractall(self.options.tmpdir)

    def setup_chain(self, split=False):
        if (USE_SNAPSHOT):
            self.import_data_to_data_dir()
            os.remove(self.options.tmpdir+'/node0/regtest/debug.log') # make sure that we have only logs from this test
            os.remove(self.options.tmpdir+'/node1/regtest/debug.log')
            os.remove(self.options.tmpdir+'/node0/regtest/wallet.dat')
            os.remove(self.options.tmpdir+'/node1/regtest/wallet.dat')

        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args =
            [
                ['-debug=cert', '-debug=mempool', '-maxorphantx=10000', f'-maxmempool={NODE0_LIMIT_B / 1000000}', '-minconf=0', '-allownonstandardtx', '-datacarriersize=512', '-maxtipage=3153600000', '-whitelist=127.0.0.1'],
                ['-debug=cert', '-debug=mempool', '-maxorphantx=10000', f'-maxmempool={NODE1_LIMIT_B / 1000000}', '-minconf=0', '-allownonstandardtx', '-datacarriersize=512', '-maxtipage=3153600000']
            ])

        connect_nodes_bi(self.nodes, 0, 1)
        self.sync_all()

    def setup_test(self):
        if USE_SNAPSHOT:
            txs     = load_from_storage(self.options.tmpdir + "/txs/")
            ctxs_nr = load_from_storage(self.options.tmpdir + "/ctxs_nr/") # these are supposed to be NOT removed
            ctxs_r  = load_from_storage(self.options.tmpdir + "/ctxs_r/")  # these are supposed to be removed
            certs   = []
            for s in range(NUM_CEASING_SIDECHAINS):
                certs.append(load_from_storage(self.options.tmpdir + f"/certs/{s}/"))
            for s in range(NUM_NONCEASING_SIDECHAINS):
                certs.append(load_from_storage(self.options.tmpdir + f"/certs/{s+NUM_CEASING_SIDECHAINS}/"))
        else:
            mark_logs("Node 0 generates {} block".format(ForkHeights['NON_CEASING_SC']), self.nodes, DEBUG_MODE)
            blhash = self.nodes[0].generate(ForkHeights['NON_CEASING_SC'] + 800)[0] # TODO: reduce to match the actual number of utxos needed
            self.sync_all()

            # forward transfer amounts
            creation_amount = Decimal("50")
            small_amount    = Decimal("0.000001")

            ## Create sidechains
            mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
            sc = []
            sc.extend(create_sidechains(self.nodes, mcTest, NUM_CEASING_SIDECHAINS, ceasing = True))
            sc.extend(create_sidechains(self.nodes, mcTest, NUM_NONCEASING_SIDECHAINS, ceasing = False))
            self.nodes[0].generate(EPOCH_LENGTH) # goto end epoch
            self.sync_all()

            self.utxos = self.nodes[0].listunspent(0)
            txs, minfr, maxfr = create_big_transactions(self.nodes[0], self.options.tmpdir, self.utxos, NODE0_LIMIT_B + EPSILON)
            ctxs_nr = create_chained_transactions(self.nodes[0], self.options.tmpdir + "/ctxs_nr", self.utxos, 2 * NUM_OUT, minfr / 2, maxfr * 2)
            ctxs_r = create_chained_transactions_and_remover(self.nodes[0], self.options.tmpdir + "/ctxs_r", self.utxos, minfr / Decimal("1.1"), minfr * Decimal("1.1"))

            certs = []
            for i in range(NUM_CEASING_SIDECHAINS):
                certs.append(create_sc_certificates(self.nodes, mcTest, self.options.tmpdir, self.utxos, sc[i], i, 0, 0, CERT_FEE * (i+1), EPSILON + (NODE0_CERT_LIMIT_B * 2 / NUM_CEASING_SIDECHAINS)))

            for i in range(NUM_NONCEASING_SIDECHAINS):
                certs.append(create_sc_certificates(self.nodes, mcTest, self.options.tmpdir, self.utxos, sc[i+2], i+2, 0, 0, CERT_FEE, EPSILON)) # not possible to precompute non ceasing certificates

        return txs, ctxs_nr, ctxs_r, certs

    def assert_limits_enforced(self):
        usage0 = int(self.nodes[0].getmempoolinfo()['bytes'])
        usage1 = int(self.nodes[1].getmempoolinfo()['bytes'])
        assert(usage0 < NODE0_LIMIT_B or print(f"Limits are not enforced on node0! {usage0}"))
        assert(usage1 < NODE1_LIMIT_B or print(f"Limits are not enforced on node1! {usage1}"))


    def run_test(self):
        '''
        This test checks that the mempool size limitation behaves as expected.
        The default size limit is 400MB, but can be tweaked with the "-maxmempool" CLI parameter (with a lower bound of
        4MB). In this test we start 2 nodes, with Node0 having a limit of 5MB, and Node1 with a limit of 4MB.
        The test fills the nodes' mempools with big transactions first, and then with certificates, checking that the
        expected elements are evicted, or that new elements are rejected.
        All the transactions and certificates in the test snapshot do not belong to any wallet in use by either node.

        CHECKLIST:
        - txs can be added normally until mempool reaches the total size limit
        - new tx with fee lower than min in mempool is rejected
        - new tx with fee greater than min in mempool is accepted, min in mempool is evicted
        - [until txs occupy more than 50% of the mempool capacity] new certificates are accepted, with txs being evicted
        - [when txs occupy less than 50% of the mempool capacity] new certificate with fee lower than min in mempool is rejected
        - [when txs occupy less than 50% of the mempool capacity] new certificate with fee higher than min in mempool is accepted, min (possibly for other sc) is evicted
        - [when txs occupy less than 50% of the mempool capacity] new certificate with fee not higher than min in mempool is rejected (duplicate?)

        Q/A:
        - should non-ceasing SC certificates be evictable? YES
        - should we accept a higher quality cert (ceasing SC only) if it evicts a low-qual cert with higher fee? NO
            - what if the new certificate is the first for a given (non-ceasing?) sidechain? NO
        - should we consider custom priorities for transactions? NO
        - should we consider sidechain specific minimum fees? NO
        '''

        print("Loading snapshot...")
        txs, ctxs_nr, ctxs_r, certs = self.setup_test()

        def feeSort(e):
            return e['feerate']
        def qualitySort(e):
            return e['quality']
        txs.sort(key=feeSort)
        ctxs_nr.sort(key=feeSort)
        ctxs_r.sort(key=feeSort)
        for c in certs:
            c.sort(key=qualitySort)

        # ctxs sorted by fee
        # txs sorted by fee
        # certs sorted by quality, with certs[0] being low fee, certs[1] high fee, and certs[2] and certs[3] low fee but non ceasing

        print("Starting actual test")
        # Send chained txs first
        tx_sent = 0
        tx_fees = {}
        mark_logs("Sending chained transactions", self.nodes, DEBUG_MODE)
        ctransactions_nr = []
        while (len(ctxs_nr) > 0):
            tx = ctxs_nr.pop(0)
            tx_hex = tx['hex']
            feerate = tx['feerate']
            ctransactions_nr.append(self.nodes[0].sendrawtransaction(tx_hex, True))
            txid = self.nodes[0].decoderawtransaction(tx_hex)['txid']
            tx_fees[txid] = feerate
            tx_size = len(tx_hex)//2
            tx_sent += tx_size
            # sync here to make sure that all txs have inputs when received by node1
            self.sync_all()

        mark_logs("Filling mempools with more transactions...", self.nodes, DEBUG_MODE)
        tx_sent_size = 0
        minfeerate = MAX_FEE
        last_tx_size = 0
        usage = int(self.nodes[0].getmempoolinfo()['bytes'])
        while (usage < NODE0_LIMIT_B - last_tx_size):
            assert(len(txs) > 0)
            tx = txs.pop(len(txs)//2) # pick from the middle, use avg fee
            tx_hex = tx['hex']
            feerate = tx['feerate']
            txid = self.nodes[0].decoderawtransaction(tx_hex)['txid']
            self.nodes[0].sendrawtransaction(tx_hex, True)
            txinfo = self.nodes[0].getrawmempool(True)[txid]
            tx_fees[txid] = feerate
            if (feerate < minfeerate):
                minfeerate = feerate
            usage = int(self.nodes[0].getmempoolinfo()['bytes'])
            last_tx_size = len(tx_hex)//2
            assert_equal(last_tx_size, txinfo['size'])
            tx_sent += last_tx_size

        print(f"Mempool almost full: {usage} out of {NODE0_LIMIT_B}")
        assert_equal(int(self.nodes[0].getmempoolinfo()['bytes-for-cert']), 0)
        self.assert_limits_enforced()

        mpool = self.nodes[0].getrawmempool()
        mark_logs("Sending low fee transaction, expecting failure", self.nodes, DEBUG_MODE)
        try:
            tx = txs.pop(0)
            tx_hex = tx['hex']
            assert(tx['feerate'] <= minfeerate)
            txid = self.nodes[0].decoderawtransaction(tx_hex)['txid']
            self.nodes[0].sendrawtransaction(tx_hex, True)
            assert(False)
        except JSONRPCException as e:
            assert_equal(e.error['code'], -7)
            assert_equal(mpool, self.nodes[0].getrawmempool())
            print("Ok")

        assert_equal(int(self.nodes[0].getmempoolinfo()['bytes-for-cert']), 0)
        self.assert_limits_enforced()
        mpool = self.nodes[0].getrawmempool()
        assert(txid not in mpool)

        mark_logs("Sending chained transactions plus remover", self.nodes, DEBUG_MODE)
        ctransactions_r = []
        while (len(ctxs_r) > 0):
            tx = ctxs_r.pop(0)
            tx_hex = tx['hex']
            tx_size = len(tx_hex) // 2
            feerate = tx['feerate']
            ctransactions_r.append(self.nodes[0].sendrawtransaction(tx_hex, True))
            txid = self.nodes[0].decoderawtransaction(tx_hex)['txid']
            tx_fees[txid] = feerate
            tx_sent += tx_size
            self.assert_limits_enforced()

        mark_logs("Sending high fee transaction, expecting success", self.nodes, DEBUG_MODE)
        try:
            tx = txs.pop()
            tx_hex = tx['hex']
            feerate = tx['feerate']
            assert(feerate > minfeerate)
            txid = self.nodes[0].decoderawtransaction(tx_hex)['txid']
            self.nodes[0].sendrawtransaction(tx_hex, True)
            tx_fees[txid] = feerate
            print("Ok")
        except JSONRPCException as e:
            assert(False)

        prev_mpool = mpool
        mpool = self.nodes[0].getrawmempool()
        assert(txid in mpool)
        evicted = [x for x in prev_mpool if x not in mpool]
        for e in evicted:
            assert(tx_fees[e] <= tx['feerate'])
        assert_equal(len(mpool), len(prev_mpool)) # exactly 1 tx has been evicted (all same size)

        assert_equal(int(self.nodes[0].getmempoolinfo()['bytes-for-cert']), 0)
        self.assert_limits_enforced()

        print("Wait for high fee transaction to be present in node1 mempool")
        timeout = 5
        waiting = 0
        mpool1 = self.nodes[1].getrawmempool()
        while txid not in mpool1 and waiting < timeout:
            print("Waiting...")
            waiting += 1
            time.sleep(1)
            mpool1 = self.nodes[1].getrawmempool()

        assert(txid in mpool1)
        assert(len(mpool1) < len(mpool))
        for tx1 in mpool1:
            feerate = tx_fees[tx1]
            assert(feerate >= minfeerate or tx1 in ctransactions_nr)

        # make sure that chained transactions, known to have aggregated high fee, have not been evicted
        for ct in ctransactions_nr:
            assert(ct in mpool)
            assert(ct in mpool1)

        mark_logs("Sending low fee non ceasing certificates, expecting success", self.nodes, DEBUG_MODE)
        cert_fees = []
        cert_nc = [self.nodes[0].sendrawtransaction(certs[2][0]['hex']),
                    self.nodes[0].sendrawtransaction(certs[3][0]['hex'])]
        cert_sent_size = (len(certs[2][0]['hex']) + len(certs[3][0]['hex'])) // 2
        cert_fees.extend([certs[2][0]['feerate'], certs[3][0]['feerate']])

        mark_logs("Filling mpool with low fee certificates, expecting success and transaction eviction", self.nodes, DEBUG_MODE)
        highest_quality_sc0 = 0
        last_cert_size = 0
        min_sc0_fee = MAX_FEE
        prev_mpool = self.nodes[0].getrawmempool()
        cert_fees_sc0 = {}

        while (cert_sent_size < NODE0_CERT_LIMIT_B or usage < NODE0_LIMIT_B - last_cert_size):
            assert(len(certs[0]) > 0)
            c = certs[0].pop(0)
            high_quality_cert_sc0 = self.nodes[0].sendrawtransaction(c['hex'])
            last_cert_size = len(c['hex']) // 2
            cert_fees.append(c['feerate'])
            cert_fees_sc0[high_quality_cert_sc0] = c['feerate']
            if (c['feerate'] / last_cert_size < min_sc0_fee):
                min_sc0_fee = c['feerate']
            cert_sent_size += last_cert_size
            usage = int(self.nodes[0].getmempoolinfo()['bytes'])

        mpool = self.nodes[0].getrawmempool()
        evicted = [x for x in prev_mpool if x not in mpool]
        for e in evicted:
            assert(e in tx_fees) # check that we evicted only transactions

        mark_logs("Sending one more low fee certificate, expecting failure", self.nodes, DEBUG_MODE)
        try:
            c = certs[0].pop(0)
            self.nodes[0].sendrawtransaction(c['hex'])
            assert(False)
        except JSONRPCException as e:
            assert_equal(e.error['code'], -7)
            assert_equal(mpool, self.nodes[0].getrawmempool())
            print("Ok")

        mark_logs("Sending high fee certificates, expecting success and sc0 certificate eviction", self.nodes, DEBUG_MODE)
        cert_fees.sort()
        cert_sent_size = 0
        min_fee_sc1 = MAX_FEE
        max_fee_sc1 = Decimal("0")
        prev_mpool = self.nodes[0].getrawmempool()
        while (cert_sent_size < NODE0_CERT_LIMIT_B or int(self.nodes[0].getmempoolinfo()['bytes']) < NODE0_LIMIT_B - last_cert_size):
            assert(len(certs[1]) > 0)
            c = certs[1].pop(0)
            high_quality_cert_sc1 = self.nodes[0].sendrawtransaction(c['hex'])
            last_cert_size = len(c['hex']) // 2
            if (c['feerate'] < min_fee_sc1):
                min_fee_sc1 = c['feerate']
            elif (c['feerate'] >= max_fee_sc1):
                max_fee_sc1 = c['feerate']
                max_fee_sc1_id = high_quality_cert_sc1
            cert_fees.pop(0)
            cert_fees.append(c['feerate'])
            cert_sent_size += last_cert_size

        mpool = self.nodes[0].getrawmempool()
        evicted = [x for x in prev_mpool if x not in mpool]
        for e in evicted:
            assert(e in tx_fees or e in cert_fees_sc0 or e in cert_nc) # check that we evicted only transactions or certificates with lower fees
            if (e in tx_fees):
                assert(min_fee_sc1 > tx_fees[e])

        cert_fees.sort()

        mark_logs("Sending one more high fee certificate with lower fee, expecting failure", self.nodes, DEBUG_MODE)

        saw_rejection = False
        while len(certs[1]) > 0:
            c = certs[1].pop(0)
            cfee = c['feerate']
            try:
                self.nodes[0].sendrawtransaction(c['hex'])
                assert(cfee > cert_fees.pop(0))
            except JSONRPCException as e:
                assert_equal(e.error['code'], -7)
                assert(cfee <= cert_fees.pop(0))
                print("Ok")
                saw_rejection = True
                break

        assert(saw_rejection)

        print("Wait for last certificate to be present in node1 mempool")
        timeout = 60 # 1 min
        mpool1 = self.nodes[1].getrawmempool()
        while max_fee_sc1_id not in mpool1 and timeout >= 0:
            print("Waiting...")
            timeout -= 1
            time.sleep(1)
            mpool1 = self.nodes[1].getrawmempool()
        assert(max_fee_sc1_id in mpool1)

        for i in range(1):
            print(f"Checking composition of mpool{i}")
            mpool = self.nodes[i].getrawmempool()
            print(f"Make sure top quality sc0 cert {high_quality_cert_sc0} is not in! (Low fee)")
            assert(high_quality_cert_sc0 not in mpool)
            for ct in ctransactions_nr:
                assert(ct in mpool) # make sure that chained transactions, known to be high fee, have not been evicted

        print("Waiting for mpool1 to settle")
        timeout = 60 # 1 min
        mpool1 = self.nodes[1].getrawmempool()
        tmp = []
        while tmp != mpool1 and timeout >= 0:
            print("Waiting...")
            timeout -= 1
            time.sleep(1)
            tmp    = mpool1
            mpool1 = self.nodes[1].getrawmempool()

        assert(timeout >= 0) # if things are still changing after 1min, something is probably wrong

        self.assert_limits_enforced()
        for n in range(2):
            print(f"Final check on node{n}")
            tx_size = 0
            cert_size = 0
            for txid in self.nodes[n].getrawmempool():
                txinfo = self.nodes[n].getrawmempool(True)[txid]
                if (txinfo['isCert']):
                    cert_size += txinfo['size']
                else:
                    tx_size += txinfo['size']

            mpinfo = self.nodes[n].getmempoolinfo()
            usage = int(mpinfo['bytes'])

            assert_equal(tx_size, int(mpinfo['bytes-for-tx']))
            assert_equal(cert_size, int(mpinfo['bytes-for-cert']))

            node_limit = NODE0_LIMIT_B if n == 0 else NODE1_LIMIT_B
            node_cert_limit = NODE0_CERT_LIMIT_B if n == 0 else NODE1_CERT_LIMIT_B

            print(f"Transactions are using {tx_size} bytes")
            print(f"Certificates are using {cert_size} bytes")
            print(f"Mempool_{n} using {usage} out of {node_limit} bytes")

            assert(tx_size <= node_limit - node_cert_limit)
            assert(tx_size + cert_size <= node_limit)
            assert(tx_size + cert_size == usage)

            print("Check no ban assigned")
            for p in self.nodes[n].getpeerinfo():
                assert_equal(p['banscore'], 0)


if __name__ == '__main__':
    mempool_size_limit().main()

