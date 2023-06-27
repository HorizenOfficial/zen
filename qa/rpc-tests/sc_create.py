#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    stop_nodes, wait_bitcoinds, \
    start_nodes, sync_blocks, sync_mempools, connect_nodes_bi, mark_logs, \
    dump_sc_info_record
from test_framework.mc_test.mc_test import CertTestUtils, CSWTestUtils, SC_FIELD_SIZE, generate_random_field_element_hex
from decimal import Decimal
import pprint

NUMB_OF_NODES = 3
DEBUG_MODE = 1
SC_COINS_MAT = 2
SC_VK_SIZE = 1024

MIN_EPOCH_LENGTH = 2
MAX_EPOCH_LENGTH = 4032

DUST_THRESHOLD = 0.00000063


class FakeDict(dict):
    def __init__(self, items):
        # need to have something in the dictionary
        self['something'] = 'something'
        self._items = items
    def items(self):
        return self._items

class SCCreateTest(BitcoinTestFramework):

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = []

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
                                 extra_args=[[f"-sccoinsmaturity={SC_COINS_MAT}", '-logtimemicros=1', '-debug=sc',
                                               '-debug=py', '-debug=mempool', '-debug=net',
                                               '-debug=bench']] * NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        self.is_network_split = split
        self.sync_all()

    def try_sidechain_creation(self, node_index: int, sc_creation_params, expected_error_message: str = None):
        '''
        This is an helper function that tries to create a new sidechain with the given parameters and eventually checks
        that if fails with the expected error message (unless this last parameter is empty).
        '''

        should_fail: bool = expected_error_message is not None and expected_error_message != ""

        try:
            self.nodes[node_index].sc_create(sc_creation_params)
            if should_fail:
                assert(False)
        except JSONRPCException as e:
            assert_equal(e.error['message'], expected_error_message)

    def run_test(self):

        mark_logs(f"Node 1 generates {ForkHeights['NON_CEASING_SC']} blocks", self.nodes, DEBUG_MODE)
        self.nodes[1].generate(ForkHeights['NON_CEASING_SC'])
        self.sync_all()

        # Initialize a list of pairs (sidechain version, bool)
        sidechain_versions = [
            (0, False), # Sidechain v0
            (1, False), # Sidechain v1
            (2, False), # Sidechain v2 with ceasing behavior
            (2, True)   # Sidechain v2 with non-ceasing behavior
        ]

        # Run the tests for sidechains v0, v1, and v2 (with with both ceasing and non-ceasing behavior)
        for (sidechain_version, is_non_ceasing) in sidechain_versions:
            mark_logs(f"Running test for sidechain version {sidechain_version} {'non-ceasing' if is_non_ceasing else ''}", self.nodes, DEBUG_MODE)
            self.run_test_internal(sidechain_version, is_non_ceasing)

    def run_test_internal(self, sidechain_version: int, is_non_ceasing: bool = False):
        '''
        This test tries creating a SC with sc_create using invalid parameters and valid parameters.
        It also checks the coin mature time of the FT. For SC creation an amount of 1 ZAT is used.
        '''
        # network topology: (0)--(1)--(2)

        # The first address returned by getnewaddress() is the coinbase address, but only if the RPC command
        # is called after mining at least one block.
        node1_coinbase_address = self.nodes[1].getnewaddress()
        node1_other_address = self.nodes[1].getnewaddress()

        creation_amount = Decimal("1.0")
        fwt_amount_1 = Decimal("2.0")
        fwt_amount_2 = Decimal("2.0")
        fwt_amount_3 = Decimal("3.0")
        fwt_amount_many = fwt_amount_1 + fwt_amount_2 + fwt_amount_3

        amounts = [{"address": "dada", "amount": creation_amount}]

        #generate wCertVk and constant
        mc_cert_test = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        mc_csw_test = CSWTestUtils(self.options.tmpdir, self.options.srcdir)
        sc_name = f"sc{sidechain_version}-{'non-ceasable' if is_non_ceasing else ''}"

        withdrawalEpochLength = 0 if is_non_ceasing else 123
        to_address = "dada"
        cert_vk = mc_cert_test.generate_params(sc_name)
        csw_vk = mc_csw_test.generate_params(sc_name)
        constant = generate_random_field_element_hex()


        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 1 create SC with wrong key in input", self.nodes, DEBUG_MODE)

        cmdInput = {
            'version': sidechain_version,
            'wrong_key': withdrawalEpochLength,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk
        }

        self.try_sidechain_creation(0, cmdInput, "Invalid parameter, unknown key: wrong_key")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 1 create SC with duplicate key in input",self.nodes,DEBUG_MODE)

        cmdInput = FakeDict([
            ('version', sidechain_version),
            ('amount', creation_amount),
            ('amount', creation_amount * 2), # <-- duplicate key
            ('toaddress', to_address),
            ('wCertVk', cert_vk)
        ])

        self.try_sidechain_creation(0, cmdInput, "Duplicate key in input: amount")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 2 tries to create a SC without specifying the version", self.nodes, DEBUG_MODE)

        cmdInput = {
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk,
        }

        self.try_sidechain_creation(0, cmdInput, "Missing mandatory parameter in input: \"version\"")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 2 tries to create a SC with a version in string format", self.nodes, DEBUG_MODE)

        cmdInput = {
            'version': "1",
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk,
        }

        self.try_sidechain_creation(0, cmdInput, "JSON value is not an integer as expected")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 2 tries to create a SC with a negative version", self.nodes, DEBUG_MODE)

        cmdInput = {
            'version': -1,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk,
        }

        self.try_sidechain_creation(0, cmdInput, "Invalid sidechain version")

        # ---------------------------------------------------------------------------------------
        # For sidechain v2 this test won't fail
        if sidechain_version != 2:
            mark_logs("\nNode 1 tries to create a SC with withdrawalEpochLength set to 0", self.nodes, DEBUG_MODE)

            cmdInput = {
                'version': sidechain_version,
                'withdrawalEpochLength': 0,
                'toaddress': to_address,
                'amount': creation_amount,
                'wCertVk': cert_vk,
            }

            self.try_sidechain_creation(0, cmdInput, f"Invalid withdrawalEpochLength: minimum value allowed={MIN_EPOCH_LENGTH}")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 1 tries to create a SC with withdrawalEpochLength set to -1", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': -1,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk,
        }

        self.try_sidechain_creation(0, cmdInput, f"Invalid withdrawalEpochLength: minimum value allowed={MIN_EPOCH_LENGTH}")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 1 tries to create a SC with withdrawalEpochLength over the max limit", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': MAX_EPOCH_LENGTH + 1,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk,
        }

        self.try_sidechain_creation(0, cmdInput, f"Invalid withdrawalEpochLength: maximum value allowed={MAX_EPOCH_LENGTH}")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 1 create SC with a fromaddress expressed in a wrong format", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'fromaddress': "1BvBMSEYstWetqTFn5Au4m4GFg7xJaNVN2",
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk
        }

        self.try_sidechain_creation(0, cmdInput, f"Invalid parameter, unknown fromaddress format: {cmdInput['fromaddress']}")


        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 1 tries to create a SC with a changeaddress that does not belong to the node", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'changeaddress': "zthWZsNRTykixeceqgifx18hMMLNrNCzCzj",
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk
        }

        self.try_sidechain_creation(0, cmdInput, f"Invalid parameter, changeaddress is not mine: {cmdInput['changeaddress']}")


        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 1 tries to create a SC with empty toaddress", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': "",
            'amount': creation_amount,
            'wCertVk': cert_vk
        }

        self.try_sidechain_creation(0, cmdInput, "Invalid toaddress format: not an hex")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 2 tries to create a SC with insufficient funds", self.nodes, DEBUG_MODE)

        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk
        }

        self.try_sidechain_creation(0, cmdInput, "Insufficient transparent funds, have 0.00, need 1.00 (minconf=1)")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 2 tries to create a SC with immature funds", self.nodes, DEBUG_MODE)

        self.nodes[2].generate(1)
        self.sync_all()

        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk
        }

        self.try_sidechain_creation(0, cmdInput, "Insufficient transparent funds, have 0.00, need 1.00 (minconf=1)")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 1 tries to create a SC with null amount", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': "",
            'wCertVk': cert_vk
        }

        self.try_sidechain_creation(0, cmdInput, "Invalid amount")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with 0 amount", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': Decimal("0.0"),
            'wCertVk': cert_vk
        }

        self.try_sidechain_creation(0, cmdInput, "Invalid parameter, amount can not be null")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with negative amount", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': Decimal("-1.0"),
            'wCertVk': cert_vk
        }

        self.try_sidechain_creation(0, cmdInput, "Amount out of range")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 1 tries to create a SC with a minconf value which leds to an error", self.nodes, DEBUG_MODE)

        # Send funds to an address and mine one block (1 confirmation)
        self.nodes[1].sendtoaddress(node1_other_address, 10.001)
        self.sync_all()

        self.nodes[1].generate(1)
        self.sync_all()

        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'fromaddress': node1_other_address,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk,
            'minconf': 2
        }

        expected_error_message = f"Insufficient transparent funds for taddr[{node1_other_address}], have 0.00, need 1.00 (minconf={cmdInput['minconf']})"
        self.try_sidechain_creation(1, cmdInput, expected_error_message)

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 1 mines one block and tries to create a SC with the previous minconf value", self.nodes, DEBUG_MODE)
        # Mine one more block (to reach 2 confirmations)
        self.nodes[1].generate(1)
        self.sync_all()

        self.try_sidechain_creation(1, cmdInput)

        # Generate one block to clean the mempool
        self.nodes[1].generate(1)
        self.sync_all()

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 1 creates SC with an amount that prevents a change above the dust threshold", self.nodes, DEBUG_MODE)

        # Consolidate Node 1 balance into one single UTXO
        self.nodes[1].sendtoaddress(node1_other_address, self.nodes[1].getbalance(), "", "", True)
        self.nodes[1].generate(1)
        self.sync_all()

        amount_below_dust_threshold = 0.00000001
        assert(amount_below_dust_threshold < DUST_THRESHOLD)
        fee = 0.000025
        current_balance = float(self.nodes[1].z_getbalance(node1_other_address))
        bad_amount = round(current_balance - fee - amount_below_dust_threshold, 8) # We need to round as Zend only accepts amounts with 8 decimals at most

        cmdInput = {
            'version': sidechain_version,
            'fromaddress': node1_other_address,
            'toaddress': to_address,
            'amount': bad_amount,
            'fee': fee,
            'wCertVk': cert_vk
        }

        current_balance_string = f"{current_balance:.10f}".rstrip('0').rstrip('.')
        missing_balance_string = f"{(DUST_THRESHOLD - amount_below_dust_threshold):.10f}".rstrip('0').rstrip('.')
        invalid_amount_string = f"{amount_below_dust_threshold:.10f}".rstrip('0').rstrip('.')
        dust_threshold_string = f"{DUST_THRESHOLD:.10f}".rstrip('0').rstrip('.')
        expected_error_message = f"Insufficient transparent funds for taddr[{node1_other_address}], have {current_balance_string}, need {missing_balance_string} more to avoid creating invalid change output {invalid_amount_string} (dust threshold is {dust_threshold_string})"
        self.try_sidechain_creation(1, cmdInput, expected_error_message)

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC without the wCertVk", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'constant': constant
        }

        self.try_sidechain_creation(0, cmdInput, "Missing mandatory parameter in input: \"wCertVk\"")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with a non hex wCertVk", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': "zz" * SC_VK_SIZE,
            'constant': constant
        }

        self.try_sidechain_creation(0, cmdInput, "wCertVk: Invalid format: not an hex")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with an odd number of characters in wCertVk", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': "a" * (SC_VK_SIZE - 1)
        }

        self.try_sidechain_creation(0, cmdInput, f"wCertVk: Invalid length {len(cmdInput['wCertVk'])}, must be even (byte string)")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with too short wCertVk byte string", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': "aa" * (SC_VK_SIZE - 1)
        }

        self.try_sidechain_creation(0, cmdInput, "Invalid wCertVk")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with too long wCertVk byte string", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': "aa" * (SC_VK_SIZE + 1)
        }

        self.try_sidechain_creation(0, cmdInput, "Invalid wCertVk")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with an invalid wCertVk", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': "aa" * SC_VK_SIZE
        }

        self.try_sidechain_creation(0, cmdInput, "Invalid wCertVk")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with non hex customData", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk' : cert_vk,
            'customData': "zz" * 1024
        }

        self.try_sidechain_creation(0, cmdInput, "customData: Invalid format: not an hex")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with an odd number of characters in customData", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk' : cert_vk,
            'customData': "b" * 1023
        }

        self.try_sidechain_creation(0, cmdInput, f"customData: Invalid length {len(cmdInput['customData'])}, must be even (byte string)")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with a too long customData byte string", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk,
            'customData': "bb" * 1025
        }

        self.try_sidechain_creation(0, cmdInput, f"customData: Invalid length {len(cmdInput['customData']) // 2}, must be 1024 bytes at most")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with non hex constant", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk,
            'constant': "zz" * SC_FIELD_SIZE
        }

        self.try_sidechain_creation(0, cmdInput, "constant: Invalid format: not an hex")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with an odd number of characters in constant", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk,
            'constant': "b" * (SC_FIELD_SIZE - 1)
        }

        self.try_sidechain_creation(0, cmdInput, f"constant: Invalid length {len(cmdInput['constant'])}, must be even (byte string)")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with a too short constant byte string", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk,
            'constant': "bb" * (SC_FIELD_SIZE - 1)
        }

        self.try_sidechain_creation(0, cmdInput, f"constant: Invalid length {len(cmdInput['constant']) // 2}, must be 32 bytes")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with a too long constant byte string", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk,
            'constant': "bb" * (SC_FIELD_SIZE + 1)
        }

        self.try_sidechain_creation(0, cmdInput, f"constant: Invalid length {len(cmdInput['constant']) // 2}, must be 32 bytes")

        # ---------------------------------------------------------------------------------------
        mark_logs("\nNode 0 tries to create a SC with an invalid constant", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk,
            'constant': "aa" * SC_FIELD_SIZE
        }

        self.try_sidechain_creation(0, cmdInput, "invalid constant")

        # ---------------------------------------------------------------------------------------
        # Specific tests for non-ceasable sidechains
        # ---------------------------------------------------------------------------------------
        if is_non_ceasing:

            # ---------------------------------------------------------------------------------------
            mark_logs("\nNode 0 tries to create a non-ceasable SC with wCeasedVk", self.nodes, DEBUG_MODE)
            cmdInput = {
                'version': sidechain_version,
                'withdrawalEpochLength': withdrawalEpochLength,
                'toaddress': to_address,
                'amount': creation_amount,
                'wCertVk': cert_vk,
                'wCeasedVk': csw_vk,
                'constant': constant
            }

            self.try_sidechain_creation(0, cmdInput, "wCeasedVk is not allowed for non-ceasable sidechains")

            # ---------------------------------------------------------------------------------------
            mark_logs("\nNode 0 tries to create a non-ceasable SC with mainchainBackwardTransferRequestDataLength higher than 0", self.nodes, DEBUG_MODE)
            cmdInput = {
                'version': sidechain_version,
                'withdrawalEpochLength': withdrawalEpochLength,
                'toaddress': to_address,
                'amount': creation_amount,
                'wCertVk': cert_vk,
                'constant': constant,
                'mainchainBackwardTransferRequestDataLength': 1
            }

            self.try_sidechain_creation(0, cmdInput, "mainchainBackwardTransferRequestDataLength is not allowed for non-ceasable sidechains")

        # ---------------------------------------------------------------------------------------

        # Let's create a new addresses for the sidechain creation tests
        from_address = self.nodes[1].getnewaddress()
        change_address = self.nodes[1].getnewaddress() # This one will be used later

        initial_amount = Decimal('10.0')

        # Send some zen to the from_address
        self.nodes[1].sendtoaddress(from_address, initial_amount)
        self.nodes[1].generate(1)
        self.sync_all()

        # Node 1 creates a SC
        mark_logs("\nNode 1 creates SC", self.nodes, DEBUG_MODE)
        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'fromaddress': from_address,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk,
            'customData': "bb" * 1024,
            'constant': constant,
            'minconf': 0,
            'fee': 0
        }

        ret = self.nodes[1].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(creating_tx, 1)

        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])
        assert_equal(sidechain_version, decoded_tx['vsc_ccout'][0]['version'])
        assert_equal(withdrawalEpochLength, decoded_tx['vsc_ccout'][0]['withdrawalEpochLength'])
        assert_equal(cert_vk, decoded_tx['vsc_ccout'][0]['wCertVk'])
        assert_equal(cmdInput['customData'], decoded_tx['vsc_ccout'][0]['customData'])
        assert_equal(constant, decoded_tx['vsc_ccout'][0]['constant'])
        assert_equal(creation_amount, decoded_tx['vsc_ccout'][0]['value'])

        # Check that we have only one output that is the change sent to 'from_address' as we didn't specify any 'change_address'
        outputs = decoded_tx['vout']
        assert_equal(len(outputs), 1)
        assert_equal(outputs[0]['scriptPubKey']['addresses'][0], from_address)

        mark_logs("\n...Node0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("Verify all nodes see the new SC...", self.nodes, DEBUG_MODE)
        scinfo0 = self.nodes[0].getscinfo(scid)['items'][0]
        scinfo1 = self.nodes[1].getscinfo(scid)['items'][0]
        scinfo2 = self.nodes[2].getscinfo(scid)['items'][0]
        assert_equal(scinfo0, scinfo1)
        assert_equal(scinfo0, scinfo2)

        mark_logs("Verify fields are set as expected...", self.nodes, DEBUG_MODE)
        assert_equal(scinfo0['version'], sidechain_version)
        assert_equal(scinfo0['withdrawalEpochLength'], withdrawalEpochLength)
        assert_equal(scinfo0['wCertVk'], cert_vk)
        assert_equal(scinfo0['customData'], "bb" * 1024)
        assert_equal(scinfo0['constant'], constant)

        # Check that the creation_amount has been taken from 'from_address'
        assert_equal(self.nodes[1].z_getbalance(from_address), initial_amount - creation_amount)

        # ---------------------------------------------------------------------------------------
        # Check maturity of the coins
        curh = self.nodes[2].getblockcount()
        mark_logs("\nCheck maturiy of the coins", self.nodes, DEBUG_MODE)

        dump_sc_info_record(self.nodes[2].getscinfo(scid)['items'][0], 2, DEBUG_MODE)

        if is_non_ceasing:
            mark_logs("Check that {creation_amount} coins is immediately mature", self.nodes, DEBUG_MODE)
        else:
            mark_logs("Check that {creation_amount} coins will be mature at height={curh + 2}", self.nodes, DEBUG_MODE)
        scinfo = self.nodes[2].getscinfo(scid)['items'][0]
        assert_equal(scinfo['balance'], creation_amount if is_non_ceasing else 0)

        ia = scinfo["immatureAmounts"]

        if is_non_ceasing:
            assert_equal(len(ia), 0)

        for entry in ia:
            if entry["maturityHeight"] == curh + SC_COINS_MAT:
                assert_equal(entry["amount"], creation_amount)

        # Node 1 sends 1 amount to SC
        mark_logs("\nNode 1 sends " + str(fwt_amount_1) + " coins to SC", self.nodes, DEBUG_MODE)

        mc_return_address = self.nodes[1].getnewaddress()
        cmdInput = [{'toaddress': "abcd", 'amount': fwt_amount_1, "scid":scid, 'mcReturnAddress': mc_return_address}]
        self.nodes[1].sc_send(cmdInput)
        self.sync_all()

        # Node 1 sends 3 amounts to SC
        mark_logs("\nNode 1 sends 3 amounts to SC (tot: " + str(fwt_amount_many) + ")", self.nodes, DEBUG_MODE)

        mc_return_address = self.nodes[1].getnewaddress()

        amounts = []
        amounts.append({"toaddress": "add1", "amount": fwt_amount_1, "scid": scid, "mcReturnAddress": mc_return_address})
        amounts.append({"toaddress": "add2", "amount": fwt_amount_2, "scid": scid, "mcReturnAddress": mc_return_address})
        amounts.append({"toaddress": "add3", "amount": fwt_amount_3, "scid": scid, "mcReturnAddress": mc_return_address})

        # Check that mcReturnAddress was properly set.
        tx_id = self.nodes[1].sc_send(amounts)
        tx_obj = self.nodes[1].getrawtransaction(tx_id, 1)
        for out in tx_obj['vft_ccout']:
            assert_equal(mc_return_address, out["mcReturnAddress"], "FT mc return address is different.")
        self.sync_all()

        mark_logs("\n...Node 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        # Check maturity of the coins at actual height
        curh = self.nodes[2].getblockcount()

        dump_sc_info_record(self.nodes[2].getscinfo(scid)['items'][0], 2, DEBUG_MODE)
        count = 0

        if is_non_ceasing:
            mark_logs(f"Check that {creation_amount} coins are still mature", self.nodes, DEBUG_MODE)
            mark_logs(f"Check that {fwt_amount_many + fwt_amount_1} are immediately mature", self.nodes, DEBUG_MODE)
        else:
            mark_logs(f"Check that {creation_amount} coins will be mature at height={curh + 1}", self.nodes, DEBUG_MODE)
            mark_logs(f"Check that {fwt_amount_many + fwt_amount_1} coins will be mature at height={curh + 2}", self.nodes, DEBUG_MODE)

        scinfo = self.nodes[2].getscinfo(scid)['items'][0]
        assert_equal(scinfo['balance'], creation_amount + fwt_amount_many + fwt_amount_1 if is_non_ceasing else 0)

        ia = scinfo["immatureAmounts"]

        for entry in ia:
            count += 1
            if entry["maturityHeight"] == curh + SC_COINS_MAT:
                assert_equal(entry["amount"], fwt_amount_many + fwt_amount_1)
            if entry["maturityHeight"] == curh + SC_COINS_MAT - 1:
                assert_equal(entry["amount"], creation_amount)

        if is_non_ceasing:
            assert_equal(len(ia), 0)
        else:
            assert_equal(count, 2)

        # Check maturity of the coins at actual height+1
        mark_logs("\n...Node 0 generates 1 block", self.nodes, DEBUG_MODE)

        self.nodes[0].generate(1)
        self.sync_all()
        curh = self.nodes[2].getblockcount()

        dump_sc_info_record(self.nodes[2].getscinfo(scid)['items'][0], 2, DEBUG_MODE)
        count = 0

        if is_non_ceasing:
            mark_logs(f"Check that {fwt_amount_many + fwt_amount_1} coins are immediately mature", self.nodes, DEBUG_MODE)
        else:
            mark_logs(f"Check that {fwt_amount_many + fwt_amount_1} coins will be mature at height={curh + 1}", self.nodes, DEBUG_MODE)

        scinfo = self.nodes[2].getscinfo(scid)['items'][0]
        assert_equal(scinfo['balance'], creation_amount + fwt_amount_many + fwt_amount_1 if is_non_ceasing else creation_amount)

        ia = scinfo["immatureAmounts"]

        for entry in ia:
            if entry["maturityHeight"] == curh + SC_COINS_MAT - 1:
                assert_equal(entry["amount"], fwt_amount_many + fwt_amount_1)
                count += 1

        if is_non_ceasing:
            assert_equal(len(ia), 0)
        else:
            assert_equal(count, 1)

        # Check no immature coin at actual height+2
        mark_logs("\n...Node 0 generates 1 block", self.nodes, DEBUG_MODE)

        self.nodes[0].generate(1)
        self.sync_all()

        scinfo = self.nodes[0].getscinfo(scid, False, False)['items'][0]
        pprint.pprint(scinfo)

        mark_logs("Check that there are no immature coins", self.nodes, DEBUG_MODE)
        scinfo = self.nodes[2].getscinfo(scid)['items'][0]
        assert_equal(scinfo['balance'], creation_amount + fwt_amount_many + fwt_amount_1)

        ia = scinfo["immatureAmounts"]
        assert_equal(len(ia), 0)

        mark_logs("Checking blockindex persistance stopping and restarting nodes", self.nodes, DEBUG_MODE)
        scgeninfo = self.nodes[2].getscgenesisinfo(scid)

        stop_nodes(self.nodes)
        wait_bitcoinds()
        self.setup_network(False)

        scgeninfoPost = self.nodes[0].getscgenesisinfo(scid)
        assert_equal(scgeninfo, scgeninfoPost)

        # Let's create another sidechain, now using 'change_address'
        mark_logs("\n...Creating another sidechain specifying a \"change_address\"", self.nodes, DEBUG_MODE)

        initial_amount = self.nodes[1].z_getbalance(from_address)

        # Check that the address we use for the change is empty
        assert_equal(self.nodes[1].z_getbalance(change_address), 0)

        cmdInput = {
            'version': sidechain_version,
            'withdrawalEpochLength': withdrawalEpochLength,
            'fromaddress': from_address,
            'changeaddress': change_address,
            'toaddress': to_address,
            'amount': creation_amount,
            'wCertVk': cert_vk,
            'customData': "bb" * 1024,
            'constant': constant,
            'minconf': 0,
            'fee': 0
        }

        ret = self.nodes[1].sc_create(cmdInput)
        creating_tx = ret['txid']
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(creating_tx, 1)

        mark_logs("\n...Node 0 generates 1 block", self.nodes, DEBUG_MODE)
        self.nodes[0].generate(1)
        self.sync_all()

        mark_logs("\n...Check that we have only one output that is the change sent to 'changeaddress'", self.nodes, DEBUG_MODE)
        outputs = decoded_tx['vout']
        assert_equal(len(outputs), 1)
        assert_equal(outputs[0]['scriptPubKey']['addresses'][0], change_address)

        assert_equal(creation_amount, decoded_tx['vsc_ccout'][0]['value'])

        # Check that the creation_amount has been taken from 'from_address' and that the change went to 'changeaddress`
        # (considering also the creation amount of the previous sidechain)
        mark_logs("\n...Check that the creation amount has been taken from 'from_address'", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[1].z_getbalance(from_address), 0)

        mark_logs("\n...Check that the change has been sent to 'changeaddress'", self.nodes, DEBUG_MODE)
        assert_equal(self.nodes[1].z_getbalance(change_address), initial_amount - creation_amount)


if __name__ == '__main__':
    SCCreateTest().main()
