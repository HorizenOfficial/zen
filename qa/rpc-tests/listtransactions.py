#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Exercise the listtransactions API

from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import MINIMAL_SC_HEIGHT
from decimal import Decimal
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_equal
from test_framework.mc_test.mc_test import *


def check_array_result(object_array, to_match, expected):
    """
    Pass in array of JSON objects, a dictionary with key/value pairs
    to match against, and another dictionary with expected key/value
    pairs.
    """
    num_matched = 0
    for item in object_array:
        all_match = True
        for key, value in to_match.items():
            if item[key] != value:
                all_match = False
        if not all_match:
            continue
        for key, value in expected.items():
            if item[key] != value:
                raise AssertionError("%s : expected %s=%s" % (str(item), str(key), str(value)))
            num_matched = num_matched + 1
    if num_matched == 0:
        raise AssertionError("No objects matched %s" % (str(to_match)))


class ListTransactionsTest(BitcoinTestFramework):

    def run_test(self):
        # Simple send, 0 to 2:
        txid = self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 0.1)
        self.sync_all()
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid},
                           {"category":"send","account":"","amount":Decimal("-0.1"),"confirmations":0})
        check_array_result(self.nodes[2].listtransactions(),
                           {"txid":txid},
                           {"category":"receive","account":"","amount":Decimal("0.1"),"confirmations":0})
        # mine a block, confirmations should change:
        self.nodes[0].generate(1)
        self.sync_all()
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid},
                           {"category":"send","account":"","amount":Decimal("-0.1"),"confirmations":1})
        check_array_result(self.nodes[2].listtransactions(),
                           {"txid":txid},
                           {"category":"receive","account":"","amount":Decimal("0.1"),"confirmations":1})

        # send-to-self:
        txid = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 0.2)
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid": txid, "category": "send"},
                           {"amount": Decimal("-0.2")})
        check_array_result(self.nodes[0].listtransactions(),
                           {"txid":txid, "category":"receive"},
                           {"amount":Decimal("0.2")})

        # sendmany from node2: twice to self, twice to node0:
        send_to = { self.nodes[0].getnewaddress() : 0.11,
                   self.nodes[2].getnewaddress(): 0.22,
                    self.nodes[0].getaccountaddress("") : 0.33,
                   self.nodes[2].getaccountaddress(""): 0.44}
        txid = self.nodes[2].sendmany("", send_to)
        self.sync_all()
        check_array_result(self.nodes[2].listtransactions(),
                           {"category":"send","amount":Decimal("-0.11")},
                           {"txid":txid} )
        check_array_result(self.nodes[0].listtransactions(),
                           {"category":"receive","amount":Decimal("0.11")},
                           {"txid":txid} )
        check_array_result(self.nodes[2].listtransactions(),
                           {"category":"send","amount":Decimal("-0.22")},
                           {"txid":txid} )
        check_array_result(self.nodes[2].listtransactions(),
                           {"category":"receive","amount":Decimal("0.22")},
                           {"txid":txid} )
        check_array_result(self.nodes[2].listtransactions(),
                           {"category":"send","amount":Decimal("-0.33")},
                           {"txid":txid} )
        check_array_result(self.nodes[0].listtransactions(),
                           {"category":"receive","amount":Decimal("0.33")},
                           {"txid":txid, "account" : ""} )
        check_array_result(self.nodes[2].listtransactions(),
                           {"category":"send","amount":Decimal("-0.44")},
                           {"txid":txid, "account" : ""} )
        check_array_result(self.nodes[2].listtransactions(),
                           {"category":"receive","amount":Decimal("0.44")},
                           {"txid":txid, "account" : ""} )

        # Below tests about filtering by address
        self.nodes[0].generate(10)
        address = self.nodes[1].getnewaddress()

        # simple send 1 to address and verify listtransaction returns this tx with address in input
        txid = self.nodes[0].sendtoaddress(address, Decimal("1.0"))
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()

        check_array_result(self.nodes[1].listtransactions("*", 1, 0, False, address),
                           {"category": "receive", "amount": Decimal("1.0"), "address": address},
                           {"txid": txid})
        check_array_result(self.nodes[0].listtransactions("*", 1, 0, False, address),
                           {"category": "send", "amount": Decimal("-1.0"), "address": address},
                           {"txid": txid})

        # verify listtransactions returns this tx without any input
        check_array_result(self.nodes[1].listtransactions("*"),
                           {"txid": txid},
                           {"category": "receive", "amount": Decimal("1.0"), "address": address})
        check_array_result(self.nodes[0].listtransactions("*"),
                           {"txid": txid},
                           {"category": "send", "amount": Decimal("-1.0"), "address": address})

        # verify listtransactions returns only the tx with a specific address
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), Decimal("1.0"))
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        result_node1 = self.nodes[1].listtransactions("*", 1, 0, False, address)
        result_node0 = self.nodes[0].listtransactions("*", 1, 0, False, address)
        if (len(result_node1) != 1):
            raise AssertionError("Expected only 1 transaction")
        check_array_result(result_node1,
                           {"txid": txid},
                           {"category": "receive", "amount": Decimal("1.0"), "address": address})
        check_array_result(result_node0,
                           {"txid": txid},
                           {"category": "send", "amount": Decimal("-1.0"), "address": address})

        # verify listtransactions returns 10 tx with no inputs
        result_node1 = self.nodes[1].listtransactions("*")

        if (len(result_node1) != 10):
            raise AssertionError("Expected 10 transactions")

        # verify listtransactions returns only last 10 tx with address in input
        txes = []
        for i in range(2, 12):
            txid = self.nodes[0].sendtoaddress(address, Decimal(str(i)))
            txes.append(txid)

            self.sync_all()
            self.nodes[2].generate(1)
            self.sync_all()

        # verify listtransactions returns the 5 most recent transactions of address
        result_node1 = self.nodes[1].listtransactions("*", 5, 0, False, address)
        result_node0 = self.nodes[0].listtransactions("*", 5, 0, False, address)
        if (len(result_node1) != 5):
            raise AssertionError("Expected only 5 transactions")

        for i in range(1, 6):
            check_array_result([result_node1[i - 1]],
                               {"txid": txes[4 + i]},
                               {"category": "receive", "amount": Decimal(str(i + 6)), "address": address})
            check_array_result([result_node0[i - 1]],
                               {"txid": txes[4 + i]},
                               {"category": "send", "amount": Decimal("-"+str(i + 6)), "address": address})

        # verify listtransactions returns the transactions n.3-4-5-6-7 of address
        result_node1 = self.nodes[1].listtransactions("*", 5, 3, False, address)
        result_node0 = self.nodes[0].listtransactions("*", 5, 3, False, address)
        if (len(result_node1) != 5):
            raise AssertionError("Expected only transactions: 3-4-5-6-7")

        for i in range(4, 9):
            check_array_result([result_node1[i - 4]],
                               {"txid": txes[i - 2]},
                               {"category": "receive", "amount": Decimal(str(i)), "address": address})
            check_array_result([result_node0[i - 4]],
                               {"txid": txes[i - 2]},
                               {"category": "send", "amount": Decimal("-"+str(i)), "address": address})

        # verify listtransactions returns the 5 most recent transactions
        result_node1 = self.nodes[1].listtransactions("*", 5)
        result_node0 = self.nodes[0].listtransactions("*", 5)

        if (len(result_node1) != 5):
            raise AssertionError("Expected only 5 transactions")
        for i in range(1, 6):
            check_array_result([result_node1[i - 1]],
                               {"txid": txes[4 + i]},
                               {"category": "receive", "amount": Decimal(str(i + 6))})
            check_array_result([result_node0[i - 1]],
                               {"txid": txes[4 + i]},
                               {"category": "send", "amount": Decimal("-"+str(i + 6))})

        # verify listtransactions returns the transactions n.3-4-5-6-7
        result_node1 = self.nodes[1].listtransactions("*", 5, 3)
        result_node0 = self.nodes[0].listtransactions("*", 5, 3)

        if (len(result_node1) != 5):
            raise AssertionError("Expected only transactions: 3-4-5-6-7")
        for i in range(4, 9):
            check_array_result([result_node1[i - 4]],
                               {"txid": txes[i - 2]},
                               {"amount": Decimal(str(i))})
            check_array_result([result_node0[i - 4]],
                               {"txid": txes[i - 2]},
                               {"amount": Decimal("-"+str(i))})

        # Below tests about filtering by address
        self.nodes[0].generate(10)
        self.sync_all()
        address = self.nodes[1].getnewaddress()

        # simple send 1 to address and verify listtransaction returns this tx with address in input
        txid = self.nodes[0].sendtoaddress(address, Decimal("1.0"))
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()

        check_array_result(self.nodes[1].listtransactions("*", 1, 0, False, address),
                           {"category": "receive", "amount": Decimal("1.0"), "address": address},
                           {"txid": txid})
        check_array_result(self.nodes[0].listtransactions("*", 1, 0, False, address),
                           {"category": "send", "amount": Decimal("-1.0"), "address": address},
                           {"txid": txid})

        # verify listtransactions returns this tx without any input
        check_array_result(self.nodes[1].listtransactions("*"),
                           {"txid": txid},
                           {"category": "receive", "amount": Decimal("1.0"), "address": address})
        check_array_result(self.nodes[0].listtransactions("*"),
                           {"txid": txid},
                           {"category": "send", "amount": Decimal("-1.0"), "address": address})

        # verify listtransactions returns only the tx with a specific address
        self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), Decimal("1.0"))
        self.sync_all()
        self.nodes[2].generate(1)
        self.sync_all()
        result_node1 = self.nodes[1].listtransactions("*", 1, 0, False, address)
        result_node0 = self.nodes[0].listtransactions("*", 1, 0, False, address)
        if (len(result_node1) != 1):
            raise AssertionError("Expected only 1 transaction")
        check_array_result(result_node1,
                           {"txid": txid},
                           {"category": "receive", "amount": Decimal("1.0"), "address": address})
        check_array_result(result_node0,
                           {"txid": txid},
                           {"category": "send", "amount": Decimal("-1.0"), "address": address})

        # verify listtransactions returns 10 tx with no inputs
        result_node1 = self.nodes[1].listtransactions("*")

        if (len(result_node1) != 10):
            raise AssertionError("Expected 10 transactions")

        # verify listtransactions returns only last 10 tx with address in input
        txes = []
        for i in range(2, 12):
            txid = self.nodes[0].sendtoaddress(address, Decimal(str(i)))
            txes.append(txid)

            self.sync_all()
            self.nodes[2].generate(1)
            self.sync_all()

        # verify listtransactions returns the 5 most recent transactions of address
        result_node1 = self.nodes[1].listtransactions("*", 5, 0, False, address)
        result_node0 = self.nodes[0].listtransactions("*", 5, 0, False, address)
        if (len(result_node1) != 5):
            raise AssertionError("Expected only 5 transactions")

        for i in range(1, 6):
            check_array_result([result_node1[i - 1]],
                               {"txid": txes[4 + i]},
                               {"category": "receive", "amount": Decimal(str(i + 6)), "address": address})
            check_array_result([result_node0[i - 1]],
                               {"txid": txes[4 + i]},
                               {"category": "send", "amount": Decimal("-"+str(i + 6)), "address": address})

        # verify listtransactions returns the transactions n.3-4-5-6-7 of address
        result_node1 = self.nodes[1].listtransactions("*", 5, 3, False, address)
        result_node0 = self.nodes[0].listtransactions("*", 5, 3, False, address)
        if (len(result_node1) != 5):
            raise AssertionError("Expected only transactions: 3-4-5-6-7")

        for i in range(4, 9):
            check_array_result([result_node1[i - 4]],
                               {"txid": txes[i - 2]},
                               {"category": "receive", "amount": Decimal(str(i)), "address": address})
            check_array_result([result_node0[i - 4]],
                               {"txid": txes[i - 2]},
                               {"category": "send", "amount": Decimal("-"+str(i)), "address": address})

        # verify listtransactions returns the 5 most recent transactions
        result_node1 = self.nodes[1].listtransactions("*", 5)
        result_node0 = self.nodes[0].listtransactions("*", 5)

        if (len(result_node1) != 5):
            raise AssertionError("Expected only 5 transactions")
        for i in range(1, 6):
            check_array_result([result_node1[i - 1]],
                               {"txid": txes[4 + i]},
                               {"category": "receive", "amount": Decimal(str(i + 6))})
            check_array_result([result_node0[i - 1]],
                               {"txid": txes[4 + i]},
                               {"category": "send", "amount": Decimal("-"+str(i + 6))})

        # verify listtransactions returns the transactions n.3-4-5-6-7
        result_node1 = self.nodes[1].listtransactions("*", 5, 3)
        result_node0 = self.nodes[0].listtransactions("*", 5, 3)

        if (len(result_node1) != 5):
            raise AssertionError("Expected only transactions: 3-4-5-6-7")
        for i in range(4, 9):
            check_array_result([result_node1[i - 4]],
                               {"txid": txes[i - 2]},
                               {"amount": Decimal(str(i))})
            check_array_result([result_node0[i - 4]],
                               {"txid": txes[i - 2]},
                               {"amount": Decimal("-"+str(i))})

        chain_height = self.nodes[0].getblockcount()
        if chain_height < MINIMAL_SC_HEIGHT:
            self.nodes[0].generate(MINIMAL_SC_HEIGHT - chain_height)
        self.sync_all()

        # verify we can filter sc related transactions even with an empty vout
        sc_creation_minimum_utxo = 5.0
        fromaddr = []
        sc_creation_amount = 0.0
        addr_found = False

        # select an address with an UTXO value large enough
        for groups in self.nodes[1].listaddressgroupings():
            if addr_found:
                break
            for entry in groups:
                if entry[1] >= sc_creation_minimum_utxo:
                    fromaddr = entry[0]
                    sc_creation_amount = float(entry[1])
                    addr_found = True
                    break

        assert_true(len(fromaddr))
        result_node1_latest = self.nodes[1].listtransactions("*", 1, 0, False, fromaddr)
        
        sidechain_address = "0000000000000000000000000000000000000000000000000000000051dec4a1"
        fee = 0.00025

        # avoid creating change in the resulting tx
        sc_creation_amount -= fee

        # generate vk and constant for this sidechain
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()

        cmdInput = {
            'version': 0,
            'fromaddress': fromaddr,
            'toaddress': sidechain_address,
            'amount': sc_creation_amount,
            'fee': fee,
            'wCertVk': vk,
            'constant': constant
        }
        try:
            res = self.nodes[1].sc_create(cmdInput)
            tx = res['txid']
            self.sync_all()
        except JSONRPCException, e:
            errorString = e.error['message']
            print errorString
            assert_true(False)

        # check we have no vout since we should have arranged a null change
        decoded_tx = self.nodes[1].getrawtransaction(tx, 1)
        assert_equal(0, len(decoded_tx['vout']))


if __name__ == '__main__':
    ListTransactionsTest().main()
