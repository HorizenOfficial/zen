#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights, MINER_REWARD_POST_H200
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_equal, initialize_chain_clean, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, \
    dump_ordered_tips, mark_logs
from test_framework.mc_test.mc_test import *
import os
from decimal import Decimal
import time

NUMB_OF_NODES = 3
DEBUG_MODE = 1
SC_FEE = Decimal("0.00001")


class ScInvalidateTest(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[['-logtimemicros=1', '-debug=sc', '-debug=cert'
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

    def run_test(self):
        ''' This test creates a Sidechain and forwards funds to it and then verifies
          after a fork that reverts the Sidechain creation, the forward and mbtr transfer transactions to it
          are not in mempool
        '''
        # network topology: (0)--(1)--(2)

        blocks = [self.nodes[0].getblockhash(0)]

        fwt_amount_1 = Decimal("1.0")

        # node 1 earns some coins, they would be available after 100 blocks
        mark_logs("Node 1 generates 1 block", self.nodes, DEBUG_MODE)

        blocks.extend(self.nodes[1].generate(1))
        self.sync_all()

        # node 2 earns some coins, they would be available after 100 blocks
        mark_logs("Node 2 generates 1 block", self.nodes, DEBUG_MODE)

        blocks.extend(self.nodes[2].generate(1))
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)

        blocks.extend(self.nodes[0].generate(ForkHeights['MINIMAL_SC']))
        self.sync_all()


        tx_amount = Decimal('10.00000000')
        # ---------------------------------------------------------------------------------------
        # Node 1 sends 10 coins to node 2 to have UTXO
        mark_logs("\n...Node 1 sends {} coins to node 2 to have a UTXO".format(tx_amount), self.nodes, DEBUG_MODE)

        address_ds = self.nodes[2].getnewaddress()
        txid = self.nodes[1].sendtoaddress(address_ds, tx_amount)

        self.sync_all()

        blocks.extend(self.nodes[0].generate(2))
        prev_epoch_block_hash = blocks[-1]

        self.sync_all()

        # Node 2 create rawtransaction that creates a SC using last UTXO
        mark_logs("\nNode 2 create rawtransaction that creates a SC using last UTXO...", self.nodes, DEBUG_MODE)

        decodedTx = self.nodes[2].decoderawtransaction(self.nodes[2].gettransaction(txid)['hex'])
        vout = {}
        for outpoint in decodedTx['vout']:
            if outpoint['value'] == tx_amount:
                vout = outpoint
                break

        sc_address = "0000000000000000000000000000000000000000000000000000000000000abc"
        sc_epoch = 123
        sc_cr_amount = tx_amount

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir, "darlin")
        certVk = mcTest.generate_params("sc1")
        mcTest = CSWTestUtils(self.options.tmpdir, self.options.srcdir)
        cswVk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()

        sc = [{
            "version": 0,
            "epoch_length": sc_epoch,
            "amount": sc_cr_amount,
            "address": sc_address,
            "wCertVk": certVk,
            "wCeasedVk": cswVk,
            "constant": constant,
            "mainchainBackwardTransferRequestDataLength": 1
        }]

        inputs = [{'txid': txid, 'vout': vout['n']}]
        sc_ft = []

        rawtx = self.nodes[2].createrawtransaction(inputs, {}, [], sc, sc_ft)
        sigRawtx = self.nodes[2].signrawtransaction(rawtx)

        # Node 2 create rawtransaction with same UTXO
        mark_logs("\nNode 2 create rawtransaction with same UTXO...", self.nodes, DEBUG_MODE)

        outputs = {self.nodes[0].getnewaddress(): sc_cr_amount}
        rawtx2 = self.nodes[2].createrawtransaction(inputs, outputs)
        sigRawtx2 = self.nodes[2].signrawtransaction(rawtx2)

        # Split the network: (0)--(1) / (2)
        mark_logs("\nSplit network", self.nodes, DEBUG_MODE)
        self.split_network()
        mark_logs("The network is split: 0-1 .. 2", self.nodes, DEBUG_MODE)

        # Node 0 send the SC creation transaction and generate 1 block to create it
        mark_logs("\nNode0 send the SC creation transaction", self.nodes, DEBUG_MODE)
        crTx = self.nodes[0].sendrawtransaction(sigRawtx['hex'])
        self.sync_all()

        decoded_tx = self.nodes[0].getrawtransaction(crTx, 1)
        scid = decoded_tx['vsc_ccout'][0]['scid']
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        # Node 1 creates a BWT
        mark_logs("Node1 creates a tx with a single bwt request for sc", self.nodes, DEBUG_MODE)
        totScFee = Decimal("0.0")

        fe1 = [generate_random_field_element_hex()]
        mc_dest_addr = self.nodes[1].getnewaddress()
        TX_FEE = Decimal("0.000123")
        outputs = [{'vScRequestData': fe1, 'scFee': SC_FEE, 'scid': scid, 'mcDestinationAddress':mc_dest_addr}]
        cmdParms = { "minconf":0, "fee":TX_FEE}

        try:
            mbtrTx = self.nodes[1].sc_request_transfer(outputs, cmdParms)
            mark_logs("  --> mbtrTx = {}.".format(mbtrTx), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        totScFee = totScFee + SC_FEE
        self.sync_all()

        # Node 1 creates a FT of 1.0 coin and Node 0 generates 1 block
        mark_logs("\nNode1 sends " + str(fwt_amount_1) + " coins to SC", self.nodes, DEBUG_MODE)
        mc_return_address = self.nodes[1].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount_1, "scid": scid, 'mcReturnAddress': mc_return_address}]
        ftTx = self.nodes[1].sc_send(cmdInput, {"minconf": 0})
        self.sync_all()

        assert_true(crTx in self.nodes[0].getrawmempool())
        assert_true(mbtrTx in self.nodes[0].getrawmempool())
        assert_true(ftTx in self.nodes[0].getrawmempool())

        mark_logs("\n...Node0 generating 1 block", self.nodes, DEBUG_MODE)
        blocks.extend(self.nodes[0].generate(1))
        self.sync_all()

        mark_logs("\nChecking SC info on Node 2 that should not have any SC...", self.nodes, DEBUG_MODE)
        scinfoNode0 = self.nodes[0].getscinfo(scid)['items'][0]
        scinfoNode1 = self.nodes[1].getscinfo(scid)['items'][0]
        mark_logs("Node 0: " + str(scinfoNode0), self.nodes, DEBUG_MODE)
        mark_logs("Node 1: " + str(scinfoNode1), self.nodes, DEBUG_MODE)
        assert_equal(0, self.nodes[2].getscinfo(scid)['totalItems'])

        # Node 2 generate 4 blocks including the rawtx2 and now it has the longest fork
        mark_logs("\nNode 2 generate 4 blocks including the rawtx2 and now it has the longest fork...", self.nodes, DEBUG_MODE)
        finalTx2 = self.nodes[2].sendrawtransaction(sigRawtx2['hex'])
        self.sync_all()

        self.nodes[2].generate(4)
        self.sync_all()

        mark_logs("\nJoining network", self.nodes, DEBUG_MODE)
        self.join_network()
        time.sleep(2)
        mark_logs("\nNetwork joined", self.nodes, DEBUG_MODE)

        # Checking the network chain tips
        mark_logs("\nChecking network chain tips, Node 2's fork became active...", self.nodes, DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            chaintips = self.nodes[i].getchaintips()
            dump_ordered_tips(chaintips, DEBUG_MODE)

        # Checking SC info on network
        mark_logs("\nChecking SC info on network, none see the SC...", self.nodes, DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            assert_equal(0, self.nodes[i].getscinfo(scid)['totalItems'])

        # The transactions should not be in mempool
        mark_logs("\nThe FT and MBTR transactions should not be in mempool anymore...", self.nodes, DEBUG_MODE)
        for i in range(0, NUMB_OF_NODES):
            txmem = self.nodes[i].getrawmempool()
            assert_equal(len(txmem), 0)


if __name__ == '__main__':
    ScInvalidateTest().main()
