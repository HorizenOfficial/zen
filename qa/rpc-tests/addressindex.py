#!/usr/bin/env python3
# Copyright (c) 2014-2015 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test addressindex generation and fetching
#

import binascii
import pprint
import time
from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, connect_nodes, initialize_chain_clean, start_node, to_satoshis
from test_framework.mininode import COutPoint, CTransaction, CTxIn, CTxOut
from test_framework.authproxy import JSONRPCException

class AddressIndexTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4)

    def setup_network(self):
        self.nodes = []
        # Nodes 0/1 are "wallet" nodes
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug", "-relaypriority=0"]))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-debug", "-addressindex", "-txindex"]))
        # Nodes 2/3 are used for testing
        self.nodes.append(start_node(2, self.options.tmpdir, ["-debug", "-addressindex", "-txindex", "-relaypriority=0"]))
        self.nodes.append(start_node(3, self.options.tmpdir, ["-debug", "-addressindex", "-txindex"]))
        connect_nodes(self.nodes, 0, 1)
        connect_nodes(self.nodes, 0, 2)
        connect_nodes(self.nodes, 0, 3)

        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        print("Mining blocks...")
        self.nodes[0].generate(105)
        self.sync_all()

        chain_height = self.nodes[1].getblockcount()
        assert_equal(chain_height, 105)
        assert_equal(self.nodes[1].getbalance(), 0)
        assert_equal(self.nodes[2].getbalance(), 0)

        addr1 = "zrCBKy4Uoy1X5jws6cxLqrMuE1ukuctSqfS"
        addr2 = "ztano5XjpquCJdSipz7VRGFgdLqNjXmV9cD"

        # Check that balances are correct
        balance0 = self.nodes[1].getaddressbalance(addr1)
        assert_equal(balance0["balance"], 0)

        # Check p2pkh and p2sh address indexes
        print("Testing p2pkh and p2sh address index...")

        txid0 = self.nodes[0].sendtoaddress(addr2, 10)
        self.nodes[0].generate(1)

        txidb0 = self.nodes[0].sendtoaddress(addr1, 10)
        self.nodes[0].generate(1)

        txid1 = self.nodes[0].sendtoaddress(addr2, 15)
        self.nodes[0].generate(1)

        txidb1 = self.nodes[0].sendtoaddress(addr1, 15)
        self.nodes[0].generate(1)

        txid2 = self.nodes[0].sendtoaddress(addr2, 20)
        self.nodes[0].generate(1)

        txidb2 = self.nodes[0].sendtoaddress(addr1, 20)
        self.nodes[0].generate(1)

        self.sync_all()

        txids = self.nodes[1].getaddresstxids(addr2)
        assert_equal(len(txids), 3)
        assert_equal(txids[0], txid0)
        assert_equal(txids[1], txid1)
        assert_equal(txids[2], txid2)

        txidsb = self.nodes[1].getaddresstxids(addr1)
        assert_equal(len(txidsb), 3)
        assert_equal(txidsb[0], txidb0)
        assert_equal(txidsb[1], txidb1)
        assert_equal(txidsb[2], txidb2)

        # Check that limiting by height works
        print("Testing querying txids by range of block heights..")
        height_txids = self.nodes[1].getaddresstxids({
            "addresses": [addr1],
            "start": 105,
            "end": 110
        })
        assert_equal(len(height_txids), 2)
        assert_equal(height_txids[0], txidb0)
        assert_equal(height_txids[1], txidb1)

        # Check that multiple addresses works
        multitxids = self.nodes[1].getaddresstxids({"addresses": [addr1, addr2]})
        assert_equal(len(multitxids), 6)
        assert_equal(multitxids[0], txid0)
        assert_equal(multitxids[1], txidb0)
        assert_equal(multitxids[2], txid1)
        assert_equal(multitxids[3], txidb1)
        assert_equal(multitxids[4], txid2)
        assert_equal(multitxids[5], txidb2)

        # Check that balances are correct
        balance0 = self.nodes[1].getaddressbalance(addr1)
        assert_equal(balance0["balance"], to_satoshis(45))

        # Check that outputs with the same address will only return one txid
        print("Testing for txid uniqueness...")
        op_hash160 = "a9"
        op_push_20_bytes_onto_the_stack = "14"
        addressHash = "6349a418fc4578d10a372b54b45c280cc8c4382f"
        op_equal = "87"
        genesisCbah = "20bb1acf2c1fc1228967a611c7db30632098f0c641855180b5fe23793b72eea50d00b4"
        scriptPubKey = binascii.unhexlify(op_hash160 + op_push_20_bytes_onto_the_stack + addressHash + op_equal + genesisCbah)
        unspent = self.nodes[0].listunspent()
        unspent.sort(key=lambda x: x["amount"], reverse=True)
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(unspent[0]["txid"], 16), unspent[0]["vout"]))]
        tx.vout = [CTxOut(10, scriptPubKey), CTxOut(11, scriptPubKey)]
        tx.rehash()

        signed_tx = self.nodes[0].signrawtransaction(binascii.hexlify(tx.serialize()).decode("utf-8"))
        sent_txid = self.nodes[0].sendrawtransaction(signed_tx["hex"], True)

        self.nodes[0].generate(1)
        self.sync_all()

        txidsmany = self.nodes[1].getaddresstxids(addr1)
        assert_equal(len(txidsmany), 4)
        assert_equal(txidsmany[3], sent_txid)

        # Check that balances are correct
        print("Testing balances...")
        balance0 = self.nodes[1].getaddressbalance(addr1)
        assert_equal(balance0["balance"], to_satoshis(45) + 21)

        # Check that balances are correct after spending
        print("Testing balances after spending...")
        privkey2 = "cSdkPxkAjA4HDr5VHgsebAPDEh9Gyub4HK8UJr2DFGGqKKy4K5sG"
        address2 = "ztUB6YWTcj2uUe5Rbucnc7oFevn7wCKyN63"
        op_dup = "76"
        addressHash2 = "0b2f0a0c31bfe0406b0ccc1381fdbe311946dadc"
        op_equalverify = "88"
        op_checksig = "ac"
        scriptPubKey2 = binascii.unhexlify(op_dup + op_hash160 + op_push_20_bytes_onto_the_stack + addressHash2 + op_equalverify + op_checksig + genesisCbah)
        self.nodes[0].importprivkey(privkey2)

        unspent = self.nodes[0].listunspent()
        unspent.sort(key=lambda x: x["amount"], reverse=True)
        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(unspent[0]["txid"], 16), unspent[0]["vout"]))]
        amount = to_satoshis(unspent[0]["amount"])
        tx.vout = [CTxOut(amount, scriptPubKey2)]
        tx.rehash()
        signed_tx = self.nodes[0].signrawtransaction(binascii.hexlify(tx.serialize()).decode("utf-8"))
        spending_txid = self.nodes[0].sendrawtransaction(signed_tx["hex"], True)
        self.nodes[0].generate(1)
        self.sync_all()
        balance1 = self.nodes[1].getaddressbalance(address2)
        assert_equal(balance1["balance"], amount)

        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(spending_txid, 16), 0))]
        send_amount = to_satoshis(1) + 12840
        change_amount = amount - send_amount - 10000
        tx.vout = [CTxOut(change_amount, scriptPubKey2), CTxOut(send_amount, scriptPubKey)]
        tx.rehash()

        signed_tx = self.nodes[0].signrawtransaction(binascii.hexlify(tx.serialize()).decode("utf-8"))
        sent_txid = self.nodes[0].sendrawtransaction(signed_tx["hex"], True)
        self.nodes[0].generate(1)
        self.sync_all()

        balance2 = self.nodes[1].getaddressbalance(address2)
        assert_equal(balance2["balance"], change_amount)

        # Check that deltas are returned correctly
        deltas = self.nodes[1].getaddressdeltas({"addresses": [address2], "start": 1, "end": 200})
        balance3 = 0
        for delta in deltas:
            balance3 += delta["satoshis"]
        assert_equal(balance3, change_amount)
        assert_equal(deltas[0]["address"], address2)
        assert_equal(deltas[0]["blockindex"], 1)

        # Check that entire range will be queried
        deltasAll = self.nodes[1].getaddressdeltas({"addresses": [address2]})
        assert_equal(len(deltasAll), len(deltas))

        # Check that deltas can be returned from range of block heights
        deltas = self.nodes[1].getaddressdeltas({"addresses": [address2], "start": 113, "end": 113})
        assert_equal(len(deltas), 1)

        # Check that unspent outputs can be queried
        print("Testing utxos...")
        utxos = self.nodes[1].getaddressutxos({"addresses": [address2]})
        assert_equal(len(utxos), 1)
        assert_equal(utxos[0]["satoshis"], change_amount)

        # Check that indexes will be updated with a reorg
        print("Testing reorg...")

        best_hash = self.nodes[0].getbestblockhash()
        self.nodes[0].invalidateblock(best_hash)
        self.nodes[1].invalidateblock(best_hash)
        self.nodes[2].invalidateblock(best_hash)
        self.nodes[3].invalidateblock(best_hash)
        self.sync_all()

        balance4 = self.nodes[1].getaddressbalance(address2)
        assert_equal(balance4, balance1)

        utxos2 = self.nodes[1].getaddressutxos({"addresses": [address2]})
        assert_equal(len(utxos2), 1)
        assert_equal(utxos2[0]["satoshis"], amount)

        # Check sorting of utxos
        self.nodes[2].generate(150)

        self.nodes[2].sendtoaddress(address2, 50)
        self.nodes[2].generate(1)
        self.nodes[2].sendtoaddress(address2, 50)
        self.nodes[2].generate(1)
        self.sync_all()

        utxos3 = self.nodes[1].getaddressutxos({"addresses": [address2]})
        assert_equal(len(utxos3), 3)
        assert_equal(utxos3[0]["height"], 114)
        assert_equal(utxos3[1]["height"], 264)
        assert_equal(utxos3[2]["height"], 265)

        # Check mempool indexing
        print("Testing mempool indexing...")

        privKey3 = "cVfUn53hAbRrDEuMexyfgDpZPhF7KqXpS8UZevsyTDaugB7HZ3CD"
        address3 = "ztihzFwiPbcoMVWzvMAHf37o8jw9VSHdLtC"
        addressHash3 = "aa9872b5bbcdb511d89e0e11aa27da73fd2c3f50"
        scriptPubKey3 = binascii.unhexlify(op_dup + op_hash160 + op_push_20_bytes_onto_the_stack + addressHash3 + op_equalverify + op_checksig + genesisCbah)
        #address4 = "zrJgNMHvfLY26avAQCeHk8NAQxubq7CExqH"
        scriptPubKey4 = binascii.unhexlify(op_hash160 + op_push_20_bytes_onto_the_stack + addressHash3 + op_equal + genesisCbah)
        unspent = self.nodes[2].listunspent()
        unspent.sort(key=lambda x: x["amount"], reverse=True)

        tx = CTransaction()
        tx.vin = [CTxIn(COutPoint(int(unspent[0]["txid"], 16), unspent[0]["vout"]))]
        amount = to_satoshis(unspent[0]["amount"])
        tx.vout = [CTxOut(amount, scriptPubKey3)]
        tx.rehash()
        signed_tx = self.nodes[2].signrawtransaction(binascii.hexlify(tx.serialize()).decode("utf-8"))
        memtxid1 = self.nodes[2].sendrawtransaction(signed_tx["hex"], True)
        time.sleep(2)

        tx2 = CTransaction()
        tx2.vin = [CTxIn(COutPoint(int(unspent[1]["txid"], 16), unspent[1]["vout"]))]
        amount = to_satoshis(unspent[1]["amount"])
        tx2.vout = [
            CTxOut(amount // 4, scriptPubKey3),
            CTxOut(amount // 4, scriptPubKey3),
            CTxOut(amount // 4, scriptPubKey4),
            CTxOut(amount // 4, scriptPubKey4)
        ]
        tx2.rehash()
        signed_tx2 = self.nodes[2].signrawtransaction(binascii.hexlify(tx2.serialize()).decode("utf-8"))
        memtxid2 = self.nodes[2].sendrawtransaction(signed_tx2["hex"], True)
        time.sleep(2)

        mempool = self.nodes[2].getaddressmempool({"addresses": [address3]})
        assert_equal(len(mempool), 3)
        assert_equal(mempool[0]["txid"], memtxid1)
        assert_equal(mempool[0]["address"], address3)
        assert_equal(mempool[0]["index"], 0)
        assert_equal(mempool[1]["txid"], memtxid2)
        assert_equal(mempool[1]["index"], 0)
        assert_equal(mempool[2]["txid"], memtxid2)
        assert_equal(mempool[2]["index"], 1)

        self.nodes[2].generate(1)
        self.sync_all()
        mempool2 = self.nodes[2].getaddressmempool({"addresses": [address3]})
        assert_equal(len(mempool2), 0)

        tx = CTransaction()
        tx.vin = [
            CTxIn(COutPoint(int(memtxid2, 16), 0)),
            CTxIn(COutPoint(int(memtxid2, 16), 1))
        ]
        tx.vout = [CTxOut(amount // 2 - 10000, scriptPubKey2)]
        tx.rehash()
        self.nodes[2].importprivkey(privKey3)
        signed_tx3 = self.nodes[2].signrawtransaction(binascii.hexlify(tx.serialize()).decode("utf-8"))
        self.nodes[2].sendrawtransaction(signed_tx3["hex"], True)
        time.sleep(2)

        mempool3 = self.nodes[2].getaddressmempool({"addresses": [address3]})
        assert_equal(len(mempool3), 2)
        assert_equal(mempool3[0]["prevtxid"], memtxid2)
        assert_equal(mempool3[0]["prevout"], 0)
        assert_equal(mempool3[1]["prevtxid"], memtxid2)
        assert_equal(mempool3[1]["prevout"], 1)

        # sending and receiving to the same address
        privkey1 = "cQY2s58LhzUCmEXN8jtAp1Etnijx78YRZ466w4ikX1V4UpTpbsf8"
        address1 = "ztkoUySJkS8SMoQEjR6SkSgmDXtMB531yiw"
        address1hash = "c192bff751af8efec15135d42bfeedf91a6f3e34"
        address1script = binascii.unhexlify(op_dup + op_hash160 + op_push_20_bytes_onto_the_stack + address1hash + op_equalverify + op_checksig + genesisCbah)

        self.nodes[0].sendtoaddress(address1, 10)
        self.nodes[0].generate(1)
        self.sync_all()

        utxos = self.nodes[1].getaddressutxos({"addresses": [address1]})
        assert_equal(len(utxos), 1)

        tx = CTransaction()
        tx.vin = [
            CTxIn(COutPoint(int(utxos[0]["txid"], 16), utxos[0]["outputIndex"]))
        ]
        amount = utxos[0]["satoshis"] - 1000
        tx.vout = [CTxOut(amount, address1script)]
        tx.rehash()
        self.nodes[0].importprivkey(privkey1)
        signed_tx = self.nodes[0].signrawtransaction(binascii.hexlify(tx.serialize()).decode("utf-8"))
        self.nodes[0].sendrawtransaction(signed_tx["hex"], True)

        self.sync_all()
        mempool_deltas = self.nodes[2].getaddressmempool({"addresses": [address1]})
        assert_equal(len(mempool_deltas), 2)

        # Include chaininfo in results
        print("Testing results with chain info...")

        deltas_with_info = self.nodes[1].getaddressdeltas({
            "addresses": [address2],
            "start": 1,
            "end": 200,
            "chainInfo": True
        })
        start_block_hash = self.nodes[1].getblockhash(1)
        end_block_hash = self.nodes[1].getblockhash(200)
        assert_equal(deltas_with_info["start"]["height"], 1)
        assert_equal(deltas_with_info["start"]["hash"], start_block_hash)
        assert_equal(deltas_with_info["end"]["height"], 200)
        assert_equal(deltas_with_info["end"]["hash"], end_block_hash)

        utxos_with_info = self.nodes[1].getaddressutxos({"addresses": [address2], "chainInfo": True})
        expected_tip_block_hash = self.nodes[1].getblockhash(267)
        assert_equal(utxos_with_info["height"], 267)
        assert_equal(utxos_with_info["hash"], expected_tip_block_hash)


        # Check that indexes don't get updated when checking a new block (e.g. when calling getBlockTemplate)

        # Initial balance is 0 and no index has been stored for addr3
        addr3 = self.nodes[1].getnewaddress()
        addr3_balance = self.nodes[2].getaddressbalance(addr3)
        addr3_txs = self.nodes[2].getaddresstxids(addr3)
        addr3_utxos = self.nodes[2].getaddressutxos(addr3)
        addr3_mempool = self.nodes[2].getaddressmempool(addr3)

        # The initial balance must be 0
        assert_equal(addr3_balance["balance"], 0)

        # At the beginning no address index must be stored
        assert_equal(addr3_txs, [])

        # At the beginning no unspent index must be stored
        assert_equal(addr3_utxos, [])

        # At the beginning no address mempool index must be stored
        assert_equal(addr3_mempool, [])


        # Add to mempool a transaction that sends money to addr3
        addr3_amount = 0.1
        addr3_txid = self.nodes[2].sendtoaddress(addr3, addr3_amount)
        addr3_balance = self.nodes[2].getaddressbalance(addr3)
        addr3_txs = self.nodes[2].getaddresstxids(addr3)
        addr3_utxos = self.nodes[2].getaddressutxos(addr3)
        addr3_mempool = self.nodes[2].getaddressmempool(addr3)

        # The balance must still be 0
        assert_equal(addr3_balance["balance"], 0)

        # The address index must still be empty
        assert_equal(addr3_txs, [])

        # The unspent index must still be empty
        assert_equal(addr3_utxos, [])

        # The address mempool index must contain the new transaction
        assert_equal(len(addr3_mempool), 1)
        assert_equal(addr3_mempool[0]["txid"], addr3_txid)


        # Call getBlockTemplate to trigger a call to VerifyBlock() => ConnectBlock()
        # It should not update any index
        self.nodes[2].getblocktemplate()
        addr3_balance = self.nodes[2].getaddressbalance(addr3)
        addr3_txs = self.nodes[2].getaddresstxids(addr3)
        addr3_utxos = self.nodes[2].getaddressutxos(addr3)
        addr3_mempool = self.nodes[2].getaddressmempool(addr3)

        # The balance must still be 0
        assert_equal(addr3_balance["balance"], 0)

        # The address index must still be empty
        assert_equal(addr3_txs, [])

        # The unspent index must still be empty
        assert_equal(addr3_utxos, [])

        # The address mempool index must still be empty
        assert_equal(len(addr3_mempool), 1)
        assert_equal(addr3_mempool[0]["txid"], addr3_txid)


        # Connect a new block "validating" the transaction sending money to addr3
        self.nodes[2].generate(1)
        self.sync_all()
        addr3_balance = self.nodes[2].getaddressbalance(addr3)
        addr3_txs = self.nodes[2].getaddresstxids(addr3)
        addr3_utxos = self.nodes[2].getaddressutxos(addr3)
        addr3_mempool = self.nodes[2].getaddressmempool(addr3)

        # The balance must be updated
        assert_equal(addr3_balance["balance"], 0.1 * 1e8)

        # The address index must contain only the new transaction
        assert_equal(len(addr3_txs), 1)
        assert_equal(addr3_txs[0], addr3_txid)

        # The unspent index must contain only the new transaction
        assert_equal(len(addr3_utxos), 1)
        assert_equal(addr3_utxos[0]["txid"], addr3_txid)

        # The address mempool index must be empty again
        assert_equal(addr3_mempool, [])


        print("Passed\n")


if __name__ == '__main__':
    AddressIndexTest().main()
