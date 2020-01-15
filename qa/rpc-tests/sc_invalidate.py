#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_true, assert_false, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, p2p_port
import os
from decimal import Decimal
import time

NUMB_OF_NODES = 3
SC_COINS_MAT = 2


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
                                 extra_args=[["-sccoinsmaturity=%d" % SC_COINS_MAT, '-logtimemicros=1', '-debug=sc',
                                              '-debug=py', '-debug=mempool', '-debug=net',
                                              '-debug=bench']] * NUMB_OF_NODES)

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
        # self.sync_all()
        time.sleep(2)
        self.is_network_split = False

    def dump_ordered_tips(self, tip_list):
        sorted_x = sorted(tip_list, key=lambda k: k['status'])
        c = 0
        for y in sorted_x:
            if (c == 0):
                print (y)
            else:
                print (" ", y)
            c = 1

    def dump_sc_info_record(self, info, i):
        print ("  Node %d - balance: %f" % (i, info["balance"]))
        print ("    created in block: %s (%d)" % (info["created in block"], info["created at block height"]))
        print ("    created in tx:    %s" % info["creating tx hash"])
        print ("    immature amounts:  ", info["immature amounts"])

    def dump_sc_info(self, scId=""):
        if scId != "":
            print ("scid: " + scId)
            print ("-------------------------------------------------------------------------------------")
            for i in range(0, NUMB_OF_NODES):
                try:
                    self.dump_sc_info_record(self.nodes[i].getscinfo(scId), i)
                except JSONRPCException, e:
                    print "  Node %d: ### [no such scid: %s]" % (i, scId)
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
        ''' This test creates a Sidechain and forwards funds to it and then verifies
          that scinfo is updated correctly in active chain also after blocks rollback
          and alternative fork propagations
        '''
        # network topology: (0)--(1)--(2)

        # side chain id
        scid = "22"

        creation_amount = Decimal("1.0")
        fwt_amount_1 = Decimal("1.0")
        fwt_amount_2 = Decimal("2.0")
        fwt_amount_3 = Decimal("3.0")
        fwt_amount_many = fwt_amount_1 + fwt_amount_2 + fwt_amount_3

        blocks = [self.nodes[0].getblockhash(0)]

        # node 1 earns some coins, they would be available after 100 blocks
        self.mark_logs("Node 1 generates 1 block")

        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        self.mark_logs("Node 0 generates 220 block")

        blocks.extend(self.nodes[0].generate(220))
        self.sync_all()

        pre_sc_block = blocks[-1]
        pre_sc_block_2 = blocks[-2]
        txes = []

        # ---------------------------------------------------------------------------------------
        # Nodes 1 creates the SC
        print("------------------------------")
        self.mark_logs("\nNode 1 creates SC")

        amounts = [{"address": "dada", "amount": creation_amount}]
        creating_tx_2 = self.nodes[1].sc_create(scid, 123, amounts)
        txes.append(creating_tx_2)
        self.sync_all()

        self.mark_logs("\n...Node0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        ownerBlock = blocks[-1]
        self.sync_all()

        # Node 1 creates a FT of 1.0 coin and Node 0 generates 1 block
        self.mark_logs("\nNode 1 sends " + str(fwt_amount_1) + " coins to SC")

        txes.append(self.nodes[1].sc_send("abcd", fwt_amount_1, scid))
        self.sync_all()

        self.mark_logs("\n...Node0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        # Node 1 sends 3 amounts to SC and Node 0 generates 3 blocks
        self.mark_logs("\nNode 1 sends 3 amounts to SC (tot: " + str(fwt_amount_many) + ")")

        amounts = []
        amounts.append({"address": "add1", "amount": fwt_amount_1, "scid": scid})
        amounts.append({"address": "add2", "amount": fwt_amount_2, "scid": scid})
        amounts.append({"address": "add3", "amount": fwt_amount_3, "scid": scid})
        txes.append(self.nodes[1].sc_sendmany(amounts))
        self.sync_all()

        print("\n...Node0 generating 3 blocks")
        blocks.extend(self.nodes[0].generate(3))
        self.sync_all()

        # Checking SC info on network
        print ("\nChecking SC info on network...")
        scinfoNode0 = self.nodes[0].getscinfo(scid)
        scinfoNode1 = self.nodes[1].getscinfo(scid)
        scinfoNode2 = self.nodes[2].getscinfo(scid)
        print ("Node 0: ", scinfoNode0)
        print ("Node 1: ", scinfoNode1)
        print ("Node 2: ", scinfoNode2)

        assert_equal(scinfoNode0, scinfoNode1)
        assert_equal(scinfoNode0, scinfoNode2)
        assert_equal(self.nodes[2].getscinfo(scid)["balance"], creation_amount + fwt_amount_1 + fwt_amount_many)
        assert_equal(self.nodes[2].getscinfo(scid)["created in block"], ownerBlock)
        assert_equal(self.nodes[2].getscinfo(scid)["creating tx hash"], creating_tx_2)

        # Checking network chain tips
        print ("\nChecking network chain tips...")
        for i in range(0, NUMB_OF_NODES):
            chaintips = self.nodes[i].getchaintips()
            assert_equal(len(chaintips), 1)
            self.dump_ordered_tips(chaintips)
            print ("---")

        # ---------------------------------------------------------------------------------------
        # node 2 invalidates the block just before the SC creation thus originating a chain fork
        print("------------------------------")
        self.mark_logs(
            "\nNodes invalidate the highest pre-SC block (height=%d).." % self.nodes[0].getblock(pre_sc_block, True)[
                "height"])

        try:
            print ("Invalidating " + pre_sc_block)
            self.nodes[2].invalidateblock(pre_sc_block)
        except JSONRPCException, e:
            errorString = e.error['message']
            print (errorString)

        time.sleep(3)

        # Checking the network chain tips
        print ("\nChecking network chain tips, Node 2 has a shorter fork...")
        for i in range(0, NUMB_OF_NODES):
            chaintips = self.nodes[i].getchaintips()
            if i == 2:
                assert_equal(len(chaintips), 2)
            else:
                assert_equal(len(chaintips), 1)
            self.dump_ordered_tips(chaintips)
            print ("---")

        # Node2 mempool will contain all the transactions from the blocks reverted
        print ("\nChecking mempools, Node 2 has the reverted blocks transaction...")
        for i in range(0, NUMB_OF_NODES):
            txmem = self.nodes[i].getrawmempool()
            if i != 2:
                assert_equal(len(txmem), 0)
            else:
                assert_equal(sorted(txes), sorted(txmem))

        print ("\nChecking SC info on Node 2 that should not have any SC...")
        try:
            self.nodes[2].getscinfo(scid)
        except JSONRPCException, e:
            errorString = e.error['message']
            print (errorString)
        assert_equal("scid not yet created" in errorString, True)

        # ---------------------------------------------------------------------------------------
        # the SC is recreated on the Node2 forked chain with all the balance
        print("------------------------------")
        self.mark_logs("\n...Node 2 generates %d malicious blocks..." % SC_COINS_MAT)

        blocks.extend(self.nodes[2].generate(SC_COINS_MAT))
        time.sleep(2)

        print ("\nChecking network chain tips, Node 2 propagated the fork to its peer...")
        for i in range(0, NUMB_OF_NODES):
            chaintips = self.nodes[i].getchaintips()
            if i == 0:
                assert_equal(len(chaintips), 1)
            else:
                assert_equal(len(chaintips), 2)
            self.dump_ordered_tips(chaintips)
            print ("---")

        scinfoNode0 = self.nodes[0].getscinfo(scid)
        scinfoNode1 = self.nodes[1].getscinfo(scid)
        scinfoNode2 = self.nodes[2].getscinfo(scid)

        assert_true(scinfoNode0 == scinfoNode1)
        assert_false(scinfoNode0 == scinfoNode2)

        # try to forward coins to the sc, each node has its own SC info data
        self.mark_logs("\nNode 1 sends " + str(fwt_amount_1) + " coins to SC")

        tx_after_fork = self.nodes[1].sc_send("abcd", fwt_amount_1, scid)
        txes.append(tx_after_fork)
        time.sleep(1)

        # the SC balance will be updated only in the node 2 forked chain
        self.mark_logs("\n...Node 2 generates 1 malicious blocks, its chain will have the same length as the honest...")

        blocks.extend(self.nodes[2].generate(1))
        time.sleep(1)

        print ("\nChecking SC info on the whole network, balance is updated only in Node2 fork...")
        self.dump_sc_info(scid)
        assert_equal(scinfoNode0, self.nodes[0].getscinfo(scid))
        assert_equal(scinfoNode1, self.nodes[1].getscinfo(scid))
        assert_false(scinfoNode2['balance'] == self.nodes[2].getscinfo(scid)['balance'])

        # The last tx is still in the Nodes 0,1 's mempool because the block created from Node 2 was not recognized as valid from Nodes 0,1
        print (
            "\nThe last tx is still in the Nodes 0,1 's mempool because the block created from Node 2 was not "
            "recognized as valid from Nodes 0,1")
        for i in range(0, NUMB_OF_NODES):
            txmem = self.nodes[i].getrawmempool()
            if i == 2:
                assert_equal(len(txmem), 0)
            else:
                assert_equal(len(txmem), 1)
                assert (tx_after_fork in txmem)

        # node0/1 will update the SC on their forked chain, including the tx in a different block than Node 2
        print("\n...Node0 generating 1 block")
        blocks.extend(self.nodes[0].generate(1))
        time.sleep(1)

        print ("\nChecking SC info on the whole network, balance is now updated everywhere...")
        self.dump_sc_info(scid)
        assert_false(scinfoNode0['immature amounts'] == self.nodes[0].getscinfo(scid)['immature amounts'])
        assert_false(scinfoNode1['immature amounts'] == self.nodes[1].getscinfo(scid)['immature amounts'])

        # check that the forks will have the same tx in different blocks
        tx_0_block = self.nodes[0].getrawtransaction(tx_after_fork, 1)['blockhash']
        tx_1_block = self.nodes[1].getrawtransaction(tx_after_fork, 1)['blockhash']
        tx_2_block = self.nodes[2].getrawtransaction(tx_after_fork, 1)['blockhash']
        assert_false(tx_0_block == tx_2_block)
        assert_true(tx_0_block == tx_1_block)

        print ("\nOk, last tx belongs to different blocks")
        print ("  Owner block of last tx on node 0: " + tx_0_block)
        print ("  Owner block of last tx on node 0: " + tx_1_block)
        print ("  Owner block of last tx on node 2: " + tx_2_block)

        # block 221 is the forked point on malicious chain which is now at 223, honest is now at 227 => 4 + (7*6/2+1) = 25
        # blocks are necessary to revert honest chain
        fin = self.nodes[0].getblockfinalityindex(ownerBlock)
        delta = self.nodes[0].getblockcount() - self.nodes[2].getblockcount()
        fin += delta

        self.mark_logs("\nNode 2 generates %d malicious blocks, its chain will prevail over honest one..." % fin)
        blocks.extend(self.nodes[2].generate(fin))
        self.sync_all()

        print ("\nChecking network chain tips, Node 2 fork has prevailed...")
        for i in range(0, NUMB_OF_NODES):
            chaintips = self.nodes[i].getchaintips()
            assert_equal(chaintips[0]['height'], 248)
            assert_equal(chaintips[0]['status'], "active")
            self.dump_ordered_tips(chaintips)
            print ("---")

        # the honest chain at node 0/1 has been reverted, all data should now match
        print ("\nChecking SC info on the whole network, all data match...")
        self.dump_sc_info(scid)
        assert_equal(self.nodes[0].getscinfo(scid), self.nodes[1].getscinfo(scid))
        assert_equal(self.nodes[0].getscinfo(scid), self.nodes[2].getscinfo(scid))

        # check tx is contained in the same block
        tx_0_block = self.nodes[0].getrawtransaction(tx_after_fork, 1)['blockhash']
        tx_1_block = self.nodes[1].getrawtransaction(tx_after_fork, 1)['blockhash']
        tx_2_block = self.nodes[2].getrawtransaction(tx_after_fork, 1)['blockhash']
        assert_true(tx_0_block == tx_2_block)
        assert_true(tx_1_block == tx_2_block)
        print ("\nOk, last tx belongs to the same block")
        print ("  Owner block of last tx on node 0: " + tx_0_block)
        print ("  Owner block of last tx on node 2: " + tx_2_block)

        # ---------------------------------------------------------------------------------------
        # All nodes invalidate the block just before the SC creation
        print("------------------------------")
        self.mark_logs("\nAll nodes invalidate the most recent common block (height=%d).." %
                       self.nodes[0].getblock(pre_sc_block_2, True)["height"])

        try:
            self.nodes[0].invalidateblock(pre_sc_block_2)
            self.nodes[1].invalidateblock(pre_sc_block_2)
            self.nodes[2].invalidateblock(pre_sc_block_2)
        except JSONRPCException, e:
            errorString = e.error['message']
            print (errorString)

        print ("\nChecking network chain tips, chain has been reverted...")
        for i in range(0, NUMB_OF_NODES):
            chaintips = self.nodes[i].getchaintips()
            assert_equal(len(chaintips), 3)
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print ("---")

        # the honest chain at nodes 0/1 has been reverted, all data should now match
        print "\nChecking SC info on the whole network has disappeared, all data match..."
        self.dump_sc_info(scid)

        print ("\nChecking mempools, Nodes still have sc tx in the mempool...")
        for i in range(0, NUMB_OF_NODES):
            txmem = self.nodes[i].getrawmempool()
            assert_equal(sorted(txes), sorted(txmem))

        # Nodes 0 generates 4 blocks
        self.mark_logs("\nNode 0 generates 4 blocks...")
        blocks.extend(self.nodes[0].generate(4))
        self.sync_all()
        last_inv_block = blocks[-1]

        # the honest chain at node 0/1 has been reverted, all data should now match
        print "\nChecking SC info on the whole network has appeared again, all data match..."
        scinfoNode0 = self.nodes[0].getscinfo(scid)
        scinfoNode1 = self.nodes[1].getscinfo(scid)
        scinfoNode2 = self.nodes[2].getscinfo(scid)
        print ("Node 0: ", scinfoNode0)
        print ("Node 1: ", scinfoNode1)
        print ("Node 2: ", scinfoNode2)

        assert_equal(scinfoNode0, scinfoNode1)
        assert_equal(scinfoNode1, scinfoNode2)

        # ---------------------------------------------------------------------------------------
        # nodes invalidate the block just before the SC creation
        print("------------------------------")
        self.mark_logs("\nNodes invalidate the latest block, immaturity is restored..")

        try:
            self.nodes[0].invalidateblock(last_inv_block)
            self.nodes[1].invalidateblock(last_inv_block)
            self.nodes[2].invalidateblock(last_inv_block)
        except JSONRPCException, e:
            errorString = e.error['message']
            print (errorString)

        print("Checking network chain tips")
        for i in range(0, NUMB_OF_NODES):
            self.dump_ordered_tips(self.nodes[i].getchaintips())
            print "---"

        self.dump_sc_info(scid)

        curh = self.nodes[1].getblockcount()

        # maturity has not been reached but for creation amount
        assert_equal(self.nodes[2].getscinfo(scid)["balance"], creation_amount)

        # maturity will be reached at next height for fwt_amount_many
        ia = self.nodes[2].getscinfo(scid)["immature amounts"]
        count = 0
        for entry in ia:
            count += 1
            h = entry["maturityHeight"]
            a = entry["amount"]
            # print "%d) h=%d, amount=%f " % (count, h, a)
            print "Check that %f coins will be mature at h=%d" % (a, h)
            if h == curh + 1:
                assert_equal(a, fwt_amount_many + creation_amount + fwt_amount_1)
            print "...OK"


if __name__ == '__main__':
    headers().main()
