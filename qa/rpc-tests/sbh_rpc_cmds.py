#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_true, initialize_chain_clean, \
    mark_logs, start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, \
    get_epoch_data, wait_and_assert_operationid_status, \
    swap_bytes
from test_framework.mc_test.mc_test import *
import os
import pprint
from decimal import Decimal
import time

NUMB_OF_NODES = 3
DEBUG_MODE = 1
EPOCH_LENGTH = 5
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')

class sbh_rpc_cmds(BitcoinTestFramework):

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(
            NUMB_OF_NODES, self.options.tmpdir,
            extra_args=[['-sccoinsmaturity=2', '-scproofqueuesize=0', '-logtimemicros=1', '-debug=sc',
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

        ''' This test validates the rpc cmds for SBH wallet
        '''
        amount_1           = Decimal("40.0")
        sc_creation_amount = Decimal("20.0")
        sc_fwd_amount      = Decimal("15.0")
        bwt_amount1        = Decimal("10.0")
        amount_2           = Decimal("3.0")

        txs_node1 = []

        # network topology: (0)--(1)--(2)
        mark_logs("\nNode 0 generates {} blocks".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()

        taddr_1 = self.nodes[1].getnewaddress()
        #----------------------------------------------------------------------------------------------
        tx = self.nodes[0].sendtoaddress(taddr_1, amount_1)
        self.sync_all()
        txs_node1.append(tx)
        mark_logs("\n===> Node0 sent {} coins to Node1 at addr {}".format(amount_1, taddr_1), self.nodes, DEBUG_MODE)

        mark_logs("\nNode0 generates 1 more block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        prev_epoch_block_hash = self.nodes[0].getbestblockhash()

        #generate wCertVk and constant
        certMcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        cswMcTest  = CSWTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = certMcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()

        sc_creating_height = self.nodes[0].getblockcount()+1
        sc_toaddress = "5c1dadd"
        minconf = 1
        fee = Decimal("0.000025")
        cmdInput = {
            "version": 0,
            "withdrawalEpochLength": EPOCH_LENGTH,
            "fromaddress": taddr_1,
            "toaddress": sc_toaddress,
            "amount": sc_creation_amount,
            "changeaddress":taddr_1,
            "fee": fee,
            "wCertVk": vk,
            "constant":constant
        }

        try:
            #----------------------------------------------------------------------------------------------
            res = self.nodes[1].sc_create(cmdInput)
            tx =   res['txid']
            scid = res['scid']
            txs_node1.append(tx)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(tx, 1)
        sc_id = decoded_tx['vsc_ccout'][0]['scid']
        assert_equal(scid, sc_id)

        mark_logs("\n===> Node1 created SC with {} coins and scid: {}".format(sc_creation_amount, scid), self.nodes, DEBUG_MODE)

        mark_logs("\nChecking Node1 unconfirmed data for addr {} with no zero conf changes spendability".format(taddr_1), self.nodes, DEBUG_MODE)
        ud = self.nodes[1].getunconfirmedtxdata(taddr_1, False)
        # this is the UTXO received initailly from Node0 and used as input for the SC creation
        assert_equal(ud['unconfirmedInput'], amount_1) 
        # this is the unconfirmed  change, that is = input - SC creation - fee
        unconf_change_1 = amount_1 - sc_creation_amount - fee
        assert_equal(ud['unconfirmedOutput'], unconf_change_1) 
        assert_equal(ud['unconfirmedTxApperances'], 1) 

        mark_logs("\nChecking Node1 unconfirmed data for addr {} with zero conf changes spendability".format(taddr_1), self.nodes, DEBUG_MODE)
        ud = self.nodes[1].getunconfirmedtxdata(taddr_1, True)
        assert_equal(ud['unconfirmedInput'], amount_1) 
        # the unconfiermed change is considered as spendable 
        assert_equal(ud['unconfirmedOutput'], Decimal("0.0")) 
        assert_equal(ud['unconfirmedTxApperances'], 1) 

        #--------------------------------------------------------------------------------------
        mc_return_address = self.nodes[1].getnewaddress()
        outputs = [{'toaddress': sc_toaddress, 'amount': sc_fwd_amount, "scid": scid, "mcReturnAddress": mc_return_address}]
        # if changeaddress is not specified but fromtaddress is, they are the same
        # with minconf == 0 we can use also change from the previous tx, which is still in mempool 
        cmdParms = { 'fromaddress': taddr_1, "minconf": 0, "fee": fee}

        try:
            tx = self.nodes[1].sc_send(outputs, cmdParms)
            self.sync_all()
            txs_node1.append(tx)
            mark_logs("\n===> Node 1 sent {} coins to fund the sc".format(sc_fwd_amount), self.nodes, DEBUG_MODE)
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        ud = self.nodes[1].getunconfirmedtxdata(taddr_1, False)
        mark_logs("\nChecking Node1 unconfirmed data for addr {} with no zero conf changes spendability".format(taddr_1), self.nodes, DEBUG_MODE)
        # this is the UTXO received initailly from Node0 and the change from the previous tx with the sc cr 
        assert_equal(ud['unconfirmedInput'], (amount_1 + unconf_change_1)) 
        # this is the unconfirmed change = input -fwt -fee 
        unconf_change_2 = unconf_change_1 - sc_fwd_amount - fee
        assert_equal(ud['unconfirmedOutput'], unconf_change_2) 
        assert_equal(ud['unconfirmedTxApperances'], 2) 

        ud = self.nodes[1].getunconfirmedtxdata(taddr_1, True)
        mark_logs("\nChecking Node1 unconfirmed data for addr {} with zero conf changes spendability".format(taddr_1), self.nodes, DEBUG_MODE)
        # the same
        assert_equal(ud['unconfirmedInput'], (amount_1 + unconf_change_1)) 
        # the unconfiermed change is considered as spendable 
        assert_equal(ud['unconfirmedOutput'], Decimal("0.0")) 
        assert_equal(ud['unconfirmedTxApperances'], 2) 

        mark_logs("\nNode0 generates 5 blocks to achieve end of withdrawal epochs", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(5)
        self.sync_all()

        epoch_number, epoch_cum_tree_hash, _ = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("\nepoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes, DEBUG_MODE)

        # node0 create a cert_1 for funding node1 
        bwt_address = self.nodes[1].getnewaddress()

        amounts = [{"address": bwt_address, "amount": bwt_amount1}]
        try:
            #Create proof for WCert
            quality = 1
            scid_swapped = str(swap_bytes(scid))
            
            proof = certMcTest.create_test_proof("sc1",
                                                 scid_swapped,
                                                 epoch_number,
                                                 quality,
                                                 MBTR_SC_FEE,
                                                 FT_SC_FEE,
                                                 epoch_cum_tree_hash,
                                                 constant = constant,
                                                 pks      = [bwt_address],
                                                 amounts  = [bwt_amount1])

            #----------------------------------------------------------------------------------------------
            cert_1 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE, CERT_FEE)
            mark_logs("\n===> Node 0 sent a cert for scid {} with bwd transfer of {} coins to Node1 pkh (addr {})".format(scid, bwt_amount1, bwt_address), self.nodes, DEBUG_MODE)
            #mark_logs("==> certificate is {}".format(cert_1), self.nodes, DEBUG_MODE)
            self.sync_all()
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert(False)

        mark_logs("\nChecking Node1 unconfirmed data for addr {}".format(bwt_address), self.nodes, DEBUG_MODE)
        ud1 = self.nodes[1].getunconfirmedtxdata(bwt_address, True)
        ud2 = self.nodes[1].getunconfirmedtxdata(bwt_address, False)
        assert_equal(ud1, ud2) 
        assert_equal(ud1['bwtImmatureOutput'], Decimal("0.0")) # Certs in mempool have bwts voided
        assert_equal(ud1['unconfirmedInput'], Decimal("0.0")) 
        assert_equal(ud1['unconfirmedOutput'], Decimal("0.0")) 
        assert_equal(ud1['unconfirmedTxApperances'], 0) 

        mark_logs("\nNode0 generates 1 more block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("\nChecking Node1 unconfirmed data for addr {}".format(bwt_address), self.nodes, DEBUG_MODE)
        ud3 = self.nodes[1].getunconfirmedtxdata(bwt_address, True)
        assert_equal(ud3['bwtImmatureOutput'], bwt_amount1) # Once confirmed, certs in mempool have bwts available
        assert_equal(ud3['unconfirmedInput'], Decimal("0.0")) 
        assert_equal(ud3['unconfirmedOutput'], Decimal("0.0")) 
        assert_equal(ud3['unconfirmedTxApperances'], 0)

        mark_logs("\nNode0 generates 1 more block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        bal_1 = self.nodes[1].z_getbalance(taddr_1, 1)
        utxos =  self.nodes[1].listunspent(1, 1000, [taddr_1])
        assert_equal(len(utxos), 1)
        assert_equal(utxos[0]['satoshis'], int(bal_1*100000000))

        taddr_2 = self.nodes[2].getnewaddress()
        recipients= [{"address":taddr_2, "amount": amount_2}]
        #----------------------------------------------------------------------------------------------
        myopid = self.nodes[1].z_sendmany(taddr_1, recipients, 1, fee, True)
        tx = wait_and_assert_operationid_status(self.nodes[1], myopid)
        self.sync_all()
        txs_node1.append(tx)
        mark_logs("\n===> Node1 sent {} coins to Node2 at addr {}".format(amount_2, taddr_2), self.nodes, DEBUG_MODE)

        ud = self.nodes[1].getunconfirmedtxdata(taddr_1, False)
        mark_logs("\nChecking Node1 unconfirmed data for addr {} with no zero conf changes spendability".format(taddr_1), self.nodes, DEBUG_MODE)
        assert_equal(ud['unconfirmedInput'], bal_1) 
        unconf_change_3 = bal_1 - amount_2 - fee
        assert_equal(ud['unconfirmedOutput'], unconf_change_3) 
        assert_equal(ud['unconfirmedTxApperances'], 1) 

        ud = self.nodes[2].getunconfirmedtxdata(taddr_2, False)
        mark_logs("\nChecking Node2 unconfirmed data for addr {} with no zero conf changes spendability".format(taddr_2), self.nodes, DEBUG_MODE)
        assert_equal(ud['unconfirmedInput'], Decimal("0.0")) 
        assert_equal(ud['unconfirmedOutput'], amount_2) 
        assert_equal(ud['unconfirmedTxApperances'], 1) 

        mark_logs("\nChecking Node1 total tx data for addr {}".format(taddr_1), self.nodes, DEBUG_MODE)
        ret = self.nodes[1].listtxesbyaddress(taddr_1)
        assert_equal(len(ret), len(txs_node1))

        mark_logs("\nChecking Node1 total tx data for addr {}".format(bwt_address), self.nodes, DEBUG_MODE)
        ret = self.nodes[1].listtxesbyaddress(bwt_address)
        assert_equal(len(ret), 1)

        mark_logs("\nChecking Node2 total tx data for addr {}".format(taddr_1), self.nodes, DEBUG_MODE)
        ret = self.nodes[2].listtxesbyaddress(taddr_1)
        assert_equal(len(ret), 1)

        mark_logs("\nChecking Node2 total tx data for addr {}".format(taddr_2), self.nodes, DEBUG_MODE)
        ret = self.nodes[2].listtxesbyaddress(taddr_2)
        assert_equal(len(ret), 1)



if __name__ == '__main__':
    sbh_rpc_cmds().main()
