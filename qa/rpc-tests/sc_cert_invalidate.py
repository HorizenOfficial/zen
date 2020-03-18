#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_false, assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, dump_ordered_tips
import os
from decimal import Decimal
import pprint

import time

DEBUG_MODE = 1
EPOCH_LENGTH = 5
CERT_FEE = 0.0001


class sc_cert_invalidate(BitcoinTestFramework):

    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(3, self.options.tmpdir, extra_args=
            [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-logtimemicros=1', '-disablesafemode=1']] * 3)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        sync_blocks(self.nodes[1:3])
        sync_mempools(self.nodes[1:3])
        self.is_network_split = split
        self.sync_all()

    def disconnect_nodes(self, from_connection, node_num):
        ip_port = "127.0.0.1:" + str(p2p_port(node_num))
        from_connection.disconnectnode(ip_port)
        # poll until version handshake complete to avoid race conditions
        # with transaction relaying
        while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
            time.sleep(0.1)

    def run_test(self):

        # side chain id
        scid = "1111111111111111111111111111111111111111111111111111111111111111"

        sc_txes = []
        certs = []

        # forward transfer amount
        creation_amount = Decimal("0.5")
        fwt_amount_1 = Decimal("1000.0")
        fwt_amount_2 = Decimal("1.0")
        fwt_amount_3 = Decimal("2.0")
        fwt_amount_4 = Decimal("3.0")

        bwt_amount_1 = Decimal("8.0")
        bwt_amount_2 = Decimal("8.5")

        sc_info = []
        balance_node0 = []

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)  # TODO this is not 1
        self.sync_all()

        mark_logs("Node 0 generates 220 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(220)
        self.sync_all()

        balance_node0.append(self.nodes[0].getbalance("", 0))
        sc_info.append("No SC")

        mark_logs("Node 1 creates the SC spending {} coins ...".format(creation_amount), self.nodes, DEBUG_MODE)
        creating_tx = self.nodes[0].sc_create(scid, EPOCH_LENGTH, "dada", creation_amount)

        mark_logs("creating_tx = {}".format(creating_tx), self.nodes, DEBUG_MODE)
        sc_txes.append(creating_tx)
        self.sync_all()

        mark_logs("Node0 generating 1 block", self.nodes, DEBUG_MODE)
        sc_creating_block = self.nodes[0].generate(1)[0]
        self.sync_all()
        mark_logs("  ==> height {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        sc_creating_height = self.nodes[0].getblockcount()
        balance_node0.append(self.nodes[0].getbalance("", 0))
        sc_info.append(self.nodes[0].getscinfo(scid))

        mark_logs("Node 0 performs a fwd transfer of {} coins to SC...".format(fwt_amount_1), self.nodes, DEBUG_MODE)
        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount_1, scid)
        print "fwd_tx=" + fwd_tx
        sc_txes.append(fwd_tx)
        self.sync_all()

        mark_logs("Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs("  ==> height {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        balance_node0.append(self.nodes[0].getbalance("", 0))
        sc_info.append(self.nodes[0].getscinfo(scid))

        pkh_node1 = self.nodes[1].getnewaddress("", True)
        pkh_node2 = self.nodes[2].getnewaddress("", True)

        mark_logs("Node0 generating 3 more blocks for achieving sc coins maturity", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs("  ==> height {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        balance_node0.append(self.nodes[0].getbalance("", 0))
        sc_info.append(self.nodes[0].getscinfo(scid))

        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs("  ==> height {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        balance_node0.append(self.nodes[0].getbalance("", 0))
        sc_info.append(self.nodes[0].getscinfo(scid))

        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs("  ==> height {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        balance_node0.append(self.nodes[0].getbalance("", 0))
        sc_info.append(self.nodes[0].getscinfo(scid))

        mark_logs("##### End epoch block = {}".format(self.nodes[0].getbestblockhash()), self.nodes, DEBUG_MODE)

        current_height = self.nodes[0].getblockcount()
        ep_n_0 = int((current_height - sc_creating_height + 1) / EPOCH_LENGTH) - 1
        ep_height_0 = sc_creating_height - 1 + ((ep_n_0 + 1) * EPOCH_LENGTH)
        ep_hash_0 = self.nodes[0].getblockhash(ep_height_0)

        mark_logs(("Node 0 performs a bwd transfer of %s coins to Node1 epn=%d, eph[%s]..." % (str(bwt_amount_1), ep_n_0, ep_hash_0)), self.nodes, DEBUG_MODE)
        amounts = []
        amounts.append({"pubkeyhash": pkh_node1, "amount": bwt_amount_1})
        cert = self.nodes[0].send_certificate(scid, ep_n_0, ep_hash_0, amounts, CERT_FEE)
        mark_logs("cert = {}".format(cert), self.nodes, DEBUG_MODE)
        certs.append(cert)
        self.sync_all()

        mark_logs("Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs("  ==> height {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        balance_node0.append(self.nodes[0].getbalance("", 0))
        sc_info.append(self.nodes[0].getscinfo(scid))

        mark_logs("Node 0 performs a fwd transfer of {} coins to SC...".format(fwt_amount_2), self.nodes, DEBUG_MODE)
        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount_2, scid)
        mark_logs("fwd_tx = {}".format(fwd_tx), self.nodes, DEBUG_MODE)
        sc_txes.append(fwd_tx)
        self.sync_all()

        mark_logs("Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs("  ==> height {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        balance_node0.append(self.nodes[0].getbalance("", 0))
        sc_info.append(self.nodes[0].getscinfo(scid))

        mark_logs("Node 0 performs a fwd transfer of {} coins to SC...".format(fwt_amount_3), self.nodes, DEBUG_MODE)
        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount_3, scid)
        mark_logs("fwd_tx = {}".format(fwd_tx), self.nodes, DEBUG_MODE)
        sc_txes.append(fwd_tx)
        self.sync_all()

        mark_logs("Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs("  ==> height {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        balance_node0.append(self.nodes[0].getbalance("", 0))
        sc_info.append(self.nodes[0].getscinfo(scid))

        mark_logs("Node 0 performs a fwd transfer of {} coins to SC...".format(fwt_amount_4), self.nodes, DEBUG_MODE)
        fwd_tx = self.nodes[0].sc_send("abcd", fwt_amount_4, scid)
        mark_logs("fwd_tx = {}".format(fwd_tx), self.nodes, DEBUG_MODE)
        sc_txes.append(fwd_tx)
        self.sync_all()

        mark_logs("Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs("  ==> height {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        balance_node0.append(self.nodes[0].getbalance("", 0))
        sc_info.append(self.nodes[0].getscinfo(scid))

        mark_logs("Node0 generating 3 more blocks for achieving sc coins maturity", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs("  ==> height {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        balance_node0.append(self.nodes[0].getbalance("", 0))
        sc_info.append(self.nodes[0].getscinfo(scid))

        mark_logs("##### End epoch block = {}".format(self.nodes[0].getbestblockhash()), self.nodes, DEBUG_MODE)

        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs("  ==> height {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        balance_node0.append(self.nodes[0].getbalance("", 0))
        sc_info.append(self.nodes[0].getscinfo(scid))

        current_height = self.nodes[0].getblockcount()

        ep_n_1 = int((current_height - sc_creating_height + 1) / EPOCH_LENGTH) - 1
        ep_height_1 = sc_creating_height - 1 + ((ep_n_1 + 1) * EPOCH_LENGTH)
        ep_hash_1 = self.nodes[0].getblockhash(ep_height_1)

        mark_logs(("Node 0 performs a bwd transfer of %s coins to Node1 epn=%d, eph[%s]..." % (str(bwt_amount_2), ep_n_1, ep_hash_1)), self.nodes, DEBUG_MODE)
        amounts = []
        amounts.append({"pubkeyhash": pkh_node2, "amount": bwt_amount_2})
        cert = self.nodes[0].send_certificate(scid, ep_n_1, ep_hash_1, amounts, CERT_FEE)
        mark_logs("cert = {}".format(cert), self.nodes, DEBUG_MODE)
        certs.append(cert)
        self.sync_all()
        print "mempool = ", pprint.pprint(self.nodes[0].getrawmempool())

        mark_logs("Node0 generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()
        mark_logs("  ==> height {}".format(self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        balance_node0.append(self.nodes[0].getbalance("", 0))
        sc_info.append(self.nodes[0].getscinfo(scid))
        print "mempool = ", pprint.pprint(self.nodes[0].getrawmempool())

        assert_equal(len(sc_info), len(balance_node0))

        cross_epoch_1 = False
        cross_epoch_0 = False

        # invalidate all blocks one by one
        for j in range(0, len(sc_info)):
            inv_hash = self.nodes[0].getbestblockhash()
            inv_heigth = self.nodes[0].getblockcount()
            mark_logs("Node 0 invalidates last block of height = {}".format(inv_heigth), self.nodes, DEBUG_MODE)
            self.nodes[0].invalidateblock(inv_hash)
            time.sleep(1)
            sc_info.pop()
            balance_node0.pop()

            # check that last cert is always in mempool until end epoch is reverted
            if (ep_height_1 < inv_heigth):
                assert_true(certs[1] in self.nodes[0].getrawmempool())
                mark_logs("cert[{}] is in mempool".format(certs[1]), self.nodes, DEBUG_MODE)
            else:
                assert_false(certs[1] in self.nodes[0].getrawmempool())

            # list are empty, exit loop
            if (len(sc_info) == 0):
                break

            try:
                assert_equal(self.nodes[0].getscinfo(scid), sc_info[-1])
            except JSONRPCException, e:
                errorString = e.error['message']
                print errorString
                if (inv_heigth > sc_creating_height):
                    assert(False)
                assert_true(creating_tx in self.nodes[0].getrawmempool())
                mark_logs("creating tx[{}] is in mempool".format(creating_tx), self.nodes, DEBUG_MODE)

        time.sleep(1)

        mempool = self.nodes[0].getrawmempool()

        mark_logs("check that mempool has only sc transactions (no certs)", self.nodes, DEBUG_MODE)
        for k in mempool:
            assert_equal((k in sc_txes), True)

        # if a coinbase used as input for a tx gets immature during invalidation, the tx is removed
        # from mempool and the assert fails
        # for m in sc_txes:
        #    # check that all sc transactions are in mempool
        #    assert_equal((m in mempool), True)

        sc_amount = Decimal(0.0)
        for m in mempool:
            a = self.nodes[0].gettransaction(m)['amount']
            f = self.nodes[0].gettransaction(m)['fee']
            print "amount=", a
            print "fee=   ", f
            print "___"
            sc_amount += (a + f)

        # check that amounts related to sc txes in mempool (no certs) are the diff of the balances after reverting
        # the whole serie of blocks
        print "total sc amount in mempool: ", sc_amount

        # (see above) if a coinbase used as input for a tx gets immature during invalidation, the tx is removed
        # from mempool and the assert fails
        # assert_equal(sc_amount, diff)

        h0 = self.nodes[0].getblockcount()
        h1 = self.nodes[1].getblockcount()
        delta = h1 - (h0)
        numbofblocks = int(delta * (delta + 1) / 2)

        mark_logs("Node0 generating {} more blocks for reverting the other chains".format(numbofblocks), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(numbofblocks)
        self.sync_all()

        print "\nSC info:\n", pprint.pprint(self.nodes[0].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(self.nodes[1].getscinfo(scid))
        print "\nSC info:\n", pprint.pprint(self.nodes[2].getscinfo(scid))

        print "Node0 balance: ", self.nodes[0].getbalance("", 0)
        print "Node1 balance: ", self.nodes[1].getbalance("", 0)
        print "Node2 balance: ", self.nodes[2].getbalance("", 0)
        print "=================================================="
        print

#        assert_true

        print
        for i in range(0, 3):
            chaintips = self.nodes[i].getchaintips()
            dump_ordered_tips(chaintips, DEBUG_MODE)
            mark_logs("---", self.nodes, DEBUG_MODE)


if __name__ == '__main__':
    sc_cert_invalidate().main()
