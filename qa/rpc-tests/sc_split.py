#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, p2p_port
import os
from decimal import Decimal
import time

NUMB_OF_NODES = 3
DEBUG_MODE = 0


class headers(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[['-sccoinsmaturity=0', '-logtimemicros=1', '-debug=sc', '-debug=py',
                                              '-debug=mempool', '-debug=net', '-debug=bench']] * NUMB_OF_NODES)

        if not split:
            # 1 and 2 are joint only if split==false
            connect_nodes_bi(self.nodes, 1, 2)
            sync_blocks(self.nodes[1:NUMB_OF_NODES])
            sync_mempools(self.nodes[1:NUMB_OF_NODES])

        connect_nodes_bi(self.nodes, 0, 1)
        self.is_network_split = split
        self.sync_all()

    def disconnect_nodes(self, from_connection, node_num):
        ip_port = "127.0.0.1:" + str(p2p_port(node_num))
        from_connection.disconnectnode(ip_port)
        # poll until version handshake complete to avoid race conditions
        # with transaction relaying
        while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
            time.sleep(0.1)

    def split_network(self):
        # Split the network of three nodes into nodes 0-1 and 2.
        assert not self.is_network_split
        self.disconnect_nodes(self.nodes[1], 2)
        self.disconnect_nodes(self.nodes[2], 1)
        self.is_network_split = True

    def join_network(self):
        # Join the (previously split) network pieces together: 0-1-2
        assert self.is_network_split
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 2, 1)
        time.sleep(2)
        self.is_network_split = False

    def dump_sc_info_record(self, info, i):
        if DEBUG_MODE == 0:
            return
        print ("  Node %d - balance: %f" % (i, info["balance"]))
        print ("    created in block: %s (%d)" % (info["created in block"], info["created at block height"]))
        print ("    created in tx:    %s" % info["creating tx hash"])
        print ("    immature amounts:  ", info["immature amounts"])

    def dump_sc_info(self, scId=""):
        if scId != "":
            print ("scid: %s" % scId)
            print ("-------------------------------------------------------------------------------------")
            for i in range(0, NUMB_OF_NODES):
                try:
                    self.dump_sc_info_record(self.nodes[i].getscinfo(scId), i)
                except JSONRPCException, e:
                    print ("  Node %d: ### [no such scid: %s]" % (i, scId))
        else:
            for i in range(0, NUMB_OF_NODES):
                x = self.nodes[i].getscinfo()
                for info in x:
                    self.dump_sc_info_record(info, i)

    def mark_logs(self, msg):
        print (msg)
        self.nodes[0].dbg_log(msg)
        self.nodes[1].dbg_log(msg)
        self.nodes[2].dbg_log(msg)

    def run_test(self):
        '''
        This test creates a Sidechain and forwards funds to it and then verifies
        that scinfo is updated correctly in active chain also after alternative fork propagations
        '''
        # network topology: (0)--(1)--(2)

        # side chain id
        scid = "22"

        # forward transfer amount
        creation_amount = Decimal("0.5")
        fwt_amount_1 = Decimal("0.2")
        fwt_amount_2 = Decimal("1.0")

        blocks = [self.nodes[0].getblockhash(0)]

        # node 1 earns some coins, they would be available after 100 blocks
        self.mark_logs("Node 1 generates 1 block")

        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        self.mark_logs("Node 0 generates 220 block")

        blocks.extend(self.nodes[0].generate(220))
        self.sync_all()

        # Split the network: (0)--(1) / (2)
        self.mark_logs("\nSplit network")
        self.split_network()
        self.mark_logs("The network is split: 0-1 .. 2")

        txes = []

        # ---------------------------------------------------------------------------------------
        # Nodes 1 creates the SC
        print("------------------------------")
        self.mark_logs("\nNode 1 creates the SC")

        amounts = [{"address": "dada", "amount": creation_amount}]
        tx_create = self.nodes[1].sc_create(scid, 123, amounts)
        txes.append(tx_create)
        self.sync_all()

        # Nodes 1 send 5.0 coins to a valid taddr to verify the network split
        self.mark_logs("\nNode 1 send 5.0 coins to a valid taddr to verify the network split")

        txes.append(self.nodes[1].sendtoaddress("zthXuPst7DVeePf2ZQvodgyMfQCrYf9oVx4", 5.0))
        self.sync_all()

        # Check the mempools of every nodes
        print ("\nChecking mempools...")
        for i in range(0, NUMB_OF_NODES):
            txmem = self.nodes[i].getrawmempool()
            if i == 2:
                assert_equal(len(txmem), 0)
            else:
                assert_equal(sorted(txes), sorted(txmem))

        self.mark_logs("\nNode0 generating 1 honest block")

        blocks.extend(self.nodes[0].generate(1))
        ownerBlock = blocks[-1]
        self.sync_all()

        # Node 1 creates a FT of 0.2 coins and Node 0 generates 1 block
        self.mark_logs("\nNode 1 performs a fwd transfer of " + str(fwt_amount_1) + " coins ...")
        txes.append(self.nodes[1].sc_send("abcd", fwt_amount_1, scid))

        print("\nNode0 generating 1 honest block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        # Node 1 creates a FT of 1.0 coin and Node 0 generates 1 block
        self.mark_logs("\nNode 1 performs a fwd transfer of " + str(fwt_amount_2) + " coins ...")
        txes.append(self.nodes[1].sc_send("abcd", fwt_amount_2, scid))
        self.sync_all()

        print("\nNode0 generating 1 honest block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        # Check the sc info
        print ("\nChecking sc info on 'honest' portion of network...")
        scinfoNode0 = self.nodes[0].getscinfo(scid)
        scinfoNode1 = self.nodes[1].getscinfo(scid)
        assert_equal(scinfoNode0, scinfoNode1)
        print ("Node 0: ", scinfoNode0)
        print ("Node 1: ", scinfoNode1)
        try:
            print ("Node 2: ", self.nodes[2].getscinfo(scid))
        except JSONRPCException, e:
            errorString = e.error['message']
            print (errorString)

        assert_equal(self.nodes[1].getscinfo(scid)["balance"], creation_amount + fwt_amount_1 + fwt_amount_2)
        assert_equal(self.nodes[1].getscinfo(scid)["created in block"], ownerBlock)
        assert_equal(self.nodes[1].getscinfo(scid)["creating tx hash"], tx_create)
        assert_equal("scid not yet created" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # Nodes 2 start to work on malicious chain
        print("------------------------------")
        self.mark_logs("\nNode 2 generates 4 malicious blocks, its chain will have a greater length than honest...")

        blocks.extend(self.nodes[2].generate(4))
        self.sync_all()

        self.mark_logs("\nJoining network")
        self.join_network()
        self.mark_logs("Network joined")

        print ("\nChecking that sc info on Node1 are not available anymore since tx has been reverted...")
        try:
            print (self.nodes[1].getscinfo(scid))
        except JSONRPCException, e:
            errorString = e.error['message']
            print (errorString)
        assert_equal("scid not yet created" in errorString, True)

        # Check the mempools of every nodes
        print ("\nChecking mempools...")
        for i in range(0, NUMB_OF_NODES):
            txmem = self.nodes[i].getrawmempool()
            if i == 2:
                assert_equal(len(txmem), 0)
            else:
                assert_equal(sorted(txes), sorted(txmem))

        # ---------------------------------------------------------------------------------------
        # Node 1 restores honest chain
        print("------------------------------")
        self.mark_logs("\nNode1 generating 1 honest block and restoring the SC creation...")

        blocks.extend(self.nodes[1].generate(1))
        secondOwnerBlock = blocks[-1]
        time.sleep(2)

        self.mark_logs("\nNode1 generating 1 honest block more and restoring all of SC funds...")
        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        # Check the mempools of every nodes
        print ("\nChecking mempools...")
        for i in range(0, NUMB_OF_NODES):
            txmem = self.nodes[i].getrawmempool()
            assert_equal(len(txmem), 0)

        print ("\nChecking sc info on the whole network...")
        scinfoNode0 = self.nodes[0].getscinfo(scid)
        scinfoNode1 = self.nodes[1].getscinfo(scid)
        scinfoNode2 = self.nodes[2].getscinfo(scid)

        print ("Node 0: ", scinfoNode0)
        print ("Node 1: ", scinfoNode1)
        print ("Node 2: ", scinfoNode2)

        assert_equal(scinfoNode0, scinfoNode1)
        assert_equal(scinfoNode0, scinfoNode2)
        assert_equal(self.nodes[2].getscinfo(scid)["balance"], creation_amount + fwt_amount_1 + fwt_amount_2)
        assert_equal(self.nodes[2].getscinfo(scid)["created in block"], secondOwnerBlock)
        assert_equal(self.nodes[1].getscinfo(scid)["creating tx hash"], tx_create)


if __name__ == '__main__':
    headers().main()
