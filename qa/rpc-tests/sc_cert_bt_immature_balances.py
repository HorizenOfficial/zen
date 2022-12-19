#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, get_epoch_data, str_to_hex_str, \
    sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
    assert_false, assert_true, swap_bytes
from test_framework.mc_test.mc_test import *
import os
import pprint
from decimal import Decimal

# Check RPC calls for immature balance:
#  - listunspent
#  - listaddressgroupings
#  - getbalance
#  - z_getbalanc
#  - z_gettotalbalance
#  - getunconfirmedtxdata
#  - getunconfirmedbalance
#  - getreceivedbyaddress
#  - listreceivedbyaddress
#  - listreceivedbyaccount
#  - listtransactions
#  - listsinceblock
#  - listtxesbyaddress


DEBUG_MODE = 1
NUMB_OF_NODES = 2
EPOCH_LENGTH = 5
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal('0.00015')


def check_array_result(object_array, to_match, expected, should_not_find = False):
    '''
    Pass in array of JSON objects, a dictionary with key/value pairs
    to match against, and another dictionary with expected key/value
    pairs.
    If the should_not_find flag is true, to_match should not be found in object_array
    '''
    if should_not_find == True:
        expected = { }
    num_matched = 0
    for item in object_array:
        all_match = True
        for key,value in to_match.items():
            if item[key] != value:
                all_match = False
        if not all_match:
            continue
        for key,value in expected.items():
            if item[key] != value:
                raise AssertionError("%s : expected %s=%s"%(str(item), str(key), str(value)))
            num_matched = num_matched+1
    if num_matched == 0 and should_not_find != True:
        raise AssertionError("No objects matched %s"%(str(to_match)))
    if num_matched > 0 and should_not_find == True:
        raise AssertionError("Objects was matched %s"%(str(to_match)))


class sc_cert_bt_immature_balances(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self, split=False):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=
        [['-debug=py', '-debug=sc', '-debug=mempool', '-debug=net', '-debug=cert', '-scproofqueuesize=0',
          '-logtimemicros=1']] * NUMB_OF_NODES)

        for k in range(0, NUMB_OF_NODES - 1):
            connect_nodes_bi(self.nodes, k, k + 1)

        sync_blocks(self.nodes[1:NUMB_OF_NODES])
        sync_mempools(self.nodes[1:NUMB_OF_NODES])
        self.is_network_split = split
        self.sync_all()

    def run_test(self):

        '''
        ( 1) Create a SC
        ( 2) Advance to next epoch
        ( 3) Send a cert to SC with 2 bwt to Node1 at the same address
        ( 4) checks behavior of some rpc cmd with bwt maturity info
        '''

        creation_amount = Decimal("10.0")
        bwt_amount1 = Decimal("1.5")
        bwt_amount2 = Decimal("2.0")
        bwt_amount3 = Decimal("4.0")

        mark_logs("Node 0 generates {} block".format(ForkHeights['MINIMAL_SC']), self.nodes, DEBUG_MODE)
        self.nodes[0].generate(ForkHeights['MINIMAL_SC'])
        self.sync_all()

        # generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()
        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': EPOCH_LENGTH,
            'toaddress': "dada",
            'amount': creation_amount,
            'wCertVk': vk,
            'constant': constant
        }

        # Create a SC with a budget of 10 coins
        ret = self.nodes[0].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        scid_swapped = str(swap_bytes(scid))
        mark_logs("Node 0 created the SC spending {} coins via tx {}.".format(creation_amount, creating_tx), self.nodes,
                  DEBUG_MODE)
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        mark_logs("Node0 confirms Sc creation generating 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        sc_creating_height = self.nodes[0].getblockcount()
        self.sync_all()

        mark_logs("Node0 generates 4 more blocks to achieve end of withdrawal epochs", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(4)
        self.sync_all()

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)

        bal_without_bwt = self.nodes[1].getbalance()

        # node0 create a cert_1 for funding node1
        addr_node1 = self.nodes[1].getnewaddress()
        amounts = [{"address": addr_node1, "amount": bwt_amount1}, {"address": addr_node1, "amount": bwt_amount2}]
        mark_logs("Node 0 sends a cert for scid {} with 2 bwd transfers of {} coins to Node1 address".format(scid,
                                                                                                         bwt_amount1 + bwt_amount2,
                                                                                                         addr_node1),
                  self.nodes, DEBUG_MODE)
        try:
            # Create proof for WCert
            quality = 1
            proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE,
                                             epoch_cum_tree_hash, constant, [addr_node1, addr_node1],
                                             [bwt_amount1, bwt_amount2])

            cert_1 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                                                    epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE,
                                                       CERT_FEE)
            mark_logs("==> certificate is {}".format(cert_1), self.nodes, DEBUG_MODE)
            self.sync_all()
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        # start first epoch + 2*epocs + safe guard
        bwtMaturityHeight = (sc_creating_height - 1) + 2 * EPOCH_LENGTH + 2

        # get the taddr of Node1 where the bwt is send to
        bwt_address = self.nodes[0].getrawtransaction(cert_1, 1)['vout'][1]['scriptPubKey']['addresses'][0]

        # get account for bwt_address
        bwt_account = self.nodes[1].getaccount(bwt_address)

        # ------------------------------------------------------------------
        # IN MEMPOOL CERTIFICATE
        # ------------------------------------------------------------------

        # Check unspent transactions
        unspent_utxos = self.nodes[1].listunspent()
        for utxo in unspent_utxos:
            assert_false(utxo["txid"] == cert_1)

        # Check unconfirmed balance
        assert_equal(0, self.nodes[1].getunconfirmedbalance())

        mark_logs("Check cert is in mempool", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_1 in self.nodes[1].getrawmempool())

        mark_logs("Check the output of the listtxesbyaddress cmd is as expected when cert is unconfirmed",
                  self.nodes, DEBUG_MODE)
        res = self.nodes[1].listtxesbyaddress(bwt_address)
        assert_equal(res[0]['scid'], scid)
        assert_equal(res[0]['vout'][1]['maturityHeight'], bwtMaturityHeight)

        mark_logs("Check the there are immature outputs in the unconfirmed tx data when cert is unconfirmed",
                  self.nodes, DEBUG_MODE)
        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        assert_equal(ud['bwtImmatureOutput'],
                     Decimal("0.0"))  # not bwt_amount1+bwt_amount2 because bwts in mempool are considered voided
        # unconf bwt do not contribute to unconfOutput
        assert_equal(ud['unconfirmedOutput'], Decimal("0.0"))

        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        assert_equal(ud['bwtImmatureOutput'], Decimal("0.0"))
        assert_equal(ud['unconfirmedOutput'], Decimal("0.0"))

        balance = self.nodes[1].getreceivedbyaddress(bwt_address, 0)
        assert_equal(bal_without_bwt, balance)

        balance_by_account = self.nodes[1].getreceivedbyaccount(bwt_account)
        assert_equal(bal_without_bwt, balance_by_account)

        addr_found = False
        # select an address with an UTXO value large enough
        listaddressgroupings = self.nodes[1].listaddressgroupings()
        for groups in listaddressgroupings:
            for entry in groups:
                if entry[0] == bwt_address:
                    assert_equal(0, entry[1], "listaddressgroupings: BT output amount expected to be empty")
                    pprint.pprint(entry)
                    addr_found = True
                    break

        assert_true(addr_found, "listaddressgroupings: BT outputs for address expect to be found but empty")

        mark_logs("Check that there are no immature outputs in the mempool cert gettransaction details.", self.nodes, DEBUG_MODE)
        include_immature_bts = False
        details = self.nodes[1].gettransaction(cert_1, False, include_immature_bts)['details']
        assert_equal(0, len(details), "gettransaction: mempool cert BT outputs expected to be skipped in any case (not applicable).")
        include_immature_bts = True
        details = self.nodes[1].gettransaction(cert_1, False, include_immature_bts)['details']
        assert_equal(0, len(details), "gettransaction: mempool cert BT outputs expected to be skipped in any case (not applicable).")

        # ------------------------------------------------------------------
        # IN CHAIN CERTIFICATE
        # ------------------------------------------------------------------

        mark_logs("Node0 mines cert and cert immature outputs appear the unconfirmed tx data", self.nodes, DEBUG_MODE)
        block_id = self.nodes[0].generate(1)
        self.sync_all()

        # Check unconfirmed balance
        assert_equal(0, self.nodes[1].getunconfirmedbalance())

        ud = self.nodes[0].getunconfirmedtxdata(bwt_address)
        assert_equal(ud['bwtImmatureOutput'], Decimal("0.0"))
        assert_equal(ud['unconfirmedOutput'], Decimal("0.0"))

        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        assert_equal(ud['bwtImmatureOutput'], bwt_amount1 + bwt_amount2)
        assert_equal(ud['unconfirmedOutput'], Decimal("0.0"))

        balance = self.nodes[1].getreceivedbyaddress(bwt_address)
        assert_equal(bal_without_bwt, balance)

        balance_by_account = self.nodes[1].getreceivedbyaccount(bwt_account)
        assert_equal(bal_without_bwt, balance_by_account)

        # Check unconfirmed balance
        assert_equal(0, self.nodes[0].getunconfirmedbalance())

        list_received = self.nodes[1].listreceivedbyaddress()
        check_array_result(list_received, {"address":bwt_address}, {"txids":[cert_1, cert_1]})
        assert_equal(len(list_received), 1)

        list_received = self.nodes[1].listreceivedbyaccount()
        assert_equal(len(list_received), 1)

        include_immature_bts = False
        list_transactions = self.nodes[1].listtransactions("*", 100, 0, False, "*", include_immature_bts)
        assert_equal(0, len(list_transactions), "listtransactions: no cert immature BTs expected to be accounted.")

        include_immature_bts = True
        list_transactions = self.nodes[1].listtransactions("*", 100, 0, False, "*", include_immature_bts)
        check_array_result(list_transactions, {"txid": cert_1}, {"txid": cert_1})

        include_immature_bts = False
        list_sinceblock = self.nodes[1].listsinceblock(str_to_hex_str(block_id[0]), 1, False, include_immature_bts)
        assert_equal(2, len(list_sinceblock))
        assert_equal(0, len(list_sinceblock['transactions']),
                     "listsinceblock: no cert immature BTs expected to be accounted.")

        include_immature_bts = True
        list_sinceblock = self.nodes[1].listsinceblock(str_to_hex_str(block_id[0]), 1, False, include_immature_bts)
        assert_equal(2, len(list_sinceblock))
        check_array_result(list_sinceblock['transactions'], {"txid": cert_1}, {"txid": cert_1})

        # Check unspent transactions
        unspent_utxos = self.nodes[1].listunspent()
        for utxo in unspent_utxos:
            assert_false(utxo["txid"] == cert_1)

        res = self.nodes[1].listtxesbyaddress(bwt_address)
        for entry in res:
            assert_equal(entry['scid'], scid)
            assert_equal(entry['txid'], cert_1)

        addr_found = False
        # select an address with an UTXO value large enough
        listaddressgroupings = self.nodes[1].listaddressgroupings()
        for groups in listaddressgroupings:
            for entry in groups:
                if entry[0] == bwt_address:
                    assert_equal(0, entry[1], "listaddressgroupings: BT output amount expected to be empty")
                    pprint.pprint(entry)
                    addr_found = True
                    break

        assert_true(addr_found, "listaddressgroupings: BT outputs for address expect to be found but empty")

        mark_logs("Check that there are no immature outputs in the inchain cert gettransaction details by default.", self.nodes, DEBUG_MODE)
        include_immature_bts = False
        details = self.nodes[1].gettransaction(cert_1, False, include_immature_bts)['details']
        assert_equal(0, len(details), "gettransaction: inchain cert BT immature outputs expected to be skipped because of flag = False.")

        mark_logs("Check that there are 2 immature outputs in the inchain cert gettransaction with includeImmatureBTs=True.",
                  self.nodes, DEBUG_MODE)
        include_immature_bts = True
        details = self.nodes[1].gettransaction(cert_1, False, include_immature_bts)['details']
        assert_equal(2, len(details), "gettransaction: inchain cert BT immature outputs expected to be include because of flag = True.")
        for detail in details:
            assert_equal("immature", detail["category"], "gettransaction: different category for immature Bt expected.")

        # ------------------------------------------------------------------
        # MATURE CERTIFICATE BT OUTPUTS
        # ------------------------------------------------------------------

        mark_logs("Node0 generates 4 more blocks to achieve end of withdrawal epochs", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(4)
        self.sync_all()

        epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
        mark_logs("epoch_number = {}, epoch_cum_tree_hash = {}".format(epoch_number, epoch_cum_tree_hash), self.nodes,
                  DEBUG_MODE)

        # node0 create a cert_2 for funding node1
        amounts = [{"address": addr_node1, "amount": bwt_amount3}]
        mark_logs(
            "Node 0 sends a cert for scid {} with 1 bwd transfers of {} coins to Node1 address".format(scid, bwt_amount3,
                                                                                                   addr_node1),
            self.nodes, DEBUG_MODE)
        try:
            # Create proof for WCert
            quality = 1
            proof = mcTest.create_test_proof("sc1", scid_swapped, epoch_number, quality, MBTR_SC_FEE, FT_SC_FEE,
                                             epoch_cum_tree_hash, constant, [addr_node1], [bwt_amount3])

            cert_2 = self.nodes[0].sc_send_certificate(scid, epoch_number, quality,
                                                    epoch_cum_tree_hash, proof, amounts, FT_SC_FEE, MBTR_SC_FEE,
                                                    CERT_FEE)
            mark_logs("==> certificate is {}".format(cert_2), self.nodes, DEBUG_MODE)
            self.sync_all()
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs("Send certificate failed with reason {}".format(errorString), self.nodes, DEBUG_MODE)
            assert (False)

        mark_logs("Check cert is in mempool", self.nodes, DEBUG_MODE)
        assert_equal(True, cert_2 in self.nodes[1].getrawmempool())

        # get the taddr of Node1 where the bwt is send to
        bwt_address_new = self.nodes[0].getrawtransaction(cert_2, 1)['vout'][1]['scriptPubKey']['addresses'][0]
        assert_equal(bwt_address, bwt_address_new)

        mark_logs("Check the output of the listtxesbyaddress cmd is as expected",
                  self.nodes, DEBUG_MODE)

        res = self.nodes[1].listtxesbyaddress(bwt_address)
        for entry in res:
            # same scid for both cert
            assert_equal(entry['scid'], scid)
            if entry['txid'] == cert_1:
                assert_equal(entry['vout'][1]['maturityHeight'], bwtMaturityHeight)
            if entry['txid'] == cert_2:
                assert_equal(entry['vout'][1]['maturityHeight'], bwtMaturityHeight + EPOCH_LENGTH)

        assert_true(addr_found, "listaddressgroupings: BT outputs for address expect to be found but empty")

        mark_logs("Check that unconfirmed certs bwts are not in the unconfirmed tx data", self.nodes, DEBUG_MODE)
        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        assert_equal(ud['bwtImmatureOutput'], bwt_amount1 + bwt_amount2)

        mark_logs("Check Node1 has not bwt in its balance yet", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[1].getbalance(), bal_without_bwt)
        assert_equal(self.nodes[1].z_getbalance(bwt_address), bal_without_bwt)
        assert_equal(Decimal(self.nodes[1].z_gettotalbalance()['total']), bal_without_bwt)

        mark_logs("Node0 generates 1 more block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Check Node1 still has not bwt in its balance also after 1 block is mined", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[1].getbalance(), bal_without_bwt)
        assert_equal(self.nodes[1].z_getbalance(bwt_address), bal_without_bwt)
        assert_equal(Decimal(self.nodes[1].z_gettotalbalance()['total']), bal_without_bwt)

        mark_logs("Node0 generates 1 more block attaining the maturity of the first pair of bwts", self.nodes,
                  DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # ------------------------------------------------------------------
        # CERTIFICATE 1 BT OUTPUTS ARE MATURE
        # ------------------------------------------------------------------

        mark_logs("Check Node1 now has bwts in its balance, and their maturity height is as expected", self.nodes,
                  DEBUG_MODE)
        assert_equal(self.nodes[1].getblockcount(), bwtMaturityHeight)
        assert_equal(self.nodes[1].getbalance(), bwt_amount1 + bwt_amount2)
        assert_equal(self.nodes[1].z_getbalance(bwt_address), bwt_amount1 + bwt_amount2)

        addr_found = False
        listaddressgroupings = self.nodes[1].listaddressgroupings()
        for groups in listaddressgroupings:
            if addr_found:
                break
            for entry in groups:
                if entry[0] == bwt_address:
                    assert_equal(bwt_amount1 + bwt_amount2, entry[1],
                                 "listaddressgroupings: Cert 1 BT outputs amount expected to be accounted")
                    pprint.pprint(entry)
                    addr_found = True
                    break
        assert_true(addr_found, "listaddressgroupings: BT outputs for address expect to be found but empty")

        balance = self.nodes[1].getreceivedbyaddress(bwt_address, 0)
        assert_equal(bal_without_bwt + bwt_amount1 + bwt_amount2, balance)

        balance_by_account = self.nodes[1].getreceivedbyaccount(bwt_account)
        assert_equal(bal_without_bwt + bwt_amount1 + bwt_amount2, balance_by_account)

        mark_logs("Check the output of the listtxesbyaddress cmd is not changed",
                  self.nodes, DEBUG_MODE)
        res = self.nodes[1].listtxesbyaddress(bwt_address)
        for entry in res:
            # same scid for both cert
            assert_equal(entry['scid'], scid)
            if entry['txid'] == cert_1:
                assert_equal(entry['vout'][1]['maturityHeight'], bwtMaturityHeight)
            if entry['txid'] == cert_2:
                assert_equal(entry['vout'][1]['maturityHeight'], bwtMaturityHeight + EPOCH_LENGTH)

        mark_logs("Check the there are no immature outputs in the unconfirmed tx data but the last cert bwt",
                  self.nodes, DEBUG_MODE)
        ud = self.nodes[1].getunconfirmedtxdata(bwt_address)
        assert_equal(ud['bwtImmatureOutput'], bwt_amount3)

        mark_logs("Check that there are Mature BT outputs in the inchain cert gettransaction details by default.",
                  self.nodes, DEBUG_MODE)
        include_immature_bts = False
        details = self.nodes[1].gettransaction(cert_1, False, include_immature_bts)['details']
        assert_equal(2, len(details),
                     "gettransaction: inchain cert BT Mature outputs expected to be added.")
        for detail in details:
            assert_equal("receive", detail["category"], "gettransaction: different category for Mature BT expected.")

        mark_logs("Check that there are Mature BT outputs in the inchain cert for the listtransactions command.",
                  self.nodes, DEBUG_MODE)
        include_immature_bts = False
        list_transactions = self.nodes[1].listtransactions("*", 100, 0, False, "*", include_immature_bts)
        check_array_result(list_transactions, {"txid": cert_1}, {"txid": cert_1})

        mark_logs("Check that there are Mature BT outputs in the inchain cert for the listsinceblock command.",
                  self.nodes, DEBUG_MODE)
        include_immature_bts = False
        list_sinceblock = self.nodes[1].listsinceblock(str_to_hex_str(block_id[0]), 1, False, include_immature_bts)
        assert_equal(2, len(list_sinceblock))
        check_array_result(list_sinceblock['transactions'], {"txid": cert_1}, {"txid": cert_1})

        # Check unspent transactions
        # Must see Mature BTs
        unspent_utxos = self.nodes[1].listunspent()
        assert_equal(2, len(unspent_utxos), "Only Mature BTs expected to be present.")
        for utxo in unspent_utxos:
            assert_true(utxo["txid"] == cert_1)
            assert_true(utxo["isCert"])


if __name__ == '__main__':
    sc_cert_bt_immature_balances().main()
