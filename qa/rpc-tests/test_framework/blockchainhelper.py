#!/usr/bin/env python3

import random

from decimal import Decimal

from test_framework.authproxy import JSONRPCException
from test_framework.mc_test.mc_test import CertTestUtils, CSWTestUtils
from test_framework.util import mark_logs, swap_bytes

# A helper class to accomplish some operations in a faster way
# (e.g. sidechain creation).

EXPECT_SUCCESS = False
EXPECT_FAILURE = True

""" The following bitvector is compressed and has an uncompressed size of 254 * 4 bits """
RANDOM_BITVECTOR = "021f8b08000000000002ff017f0080ff44c7e21ba1c7c0a29de006cb8074e2ba39f15abfef2525a4cbb3f235734410bda21cdab6624de769ceec818ac6c2d3a01e382e357dce1f6e9a0ff281f0fedae0efe274351db37599af457984dcf8e3ae4479e0561341adfff4746fbe274d90f6f76b8a2552a6ebb98aee918c7ceac058f4c1ae0131249546ef5e22f4187a07da02ca5b7f000000"

""" The following field element is the merkle root of the RANDOM_BITVECTOR (to be used for proof generation) """
RANDOM_BITVECTOR_FIELD_ELEMENT = "8a7d5229f440d4700d8b0343de4e14400d1cb87428abf83bd67153bf58871721"
RANDOM_FIELD_ELEMENT = "c0df1acc18002ba8781fb0405a0506a1a73075c97fbf8f74d9f4e9f74c400a00"

""" Modulus of the field used for the management of custom fields """
FIELD_MODULUS = 0x40000000000000000000000000000000038aa1276c3f59b9a14064e200000001

""" Number of bits used to represent the field modulus """
MODULUS_BITS = 255

# TODO: move this function to a proper place.
# Maybe it can take the place of generate_random_field_element_hex
# function in the MC_Test class.
def generate_random_field_element(bits_len):
    """
    Generates a random field element of the given length in bits.
    Field elements are stored in 32 bytes (256 bits) but the maximum
    allowed value is FIELD_MODULUS.
    
    This is a recursive function: if the result is a field element
    greater than the modulus, new attempts are made until a valid
    field element is found.
    """

    assert(bits_len <= MODULUS_BITS)

    # Get a random big integer of the given bits length
    field_element = random.getrandbits(bits_len)

    # If the big integer is greater than the modulus,
    # generate a new one recursively
    if field_element >= FIELD_MODULUS:
        return generate_random_field_element(bits_len)

    # Convert the field element to hexadecimal string
    field_element_hex = hex(field_element)

    # Remove the "0x" prefix and the "L" suffix
    assert(field_element_hex.startswith("0x"))
    assert(field_element_hex.endswith("L"))
    field_element_hex = field_element_hex[2:-1]

    # The field element hex string must contain an even number of characters
    if len(field_element_hex) % 2 != 0:
        field_element_hex = "0" + field_element_hex

    # Convert to little endian for usage in the blockchain
    field_element_hex = "".join(reversed([field_element_hex[i:i+2] for i in range(0, len(field_element_hex), 2)]))

    return field_element_hex

class SidechainCreationInput(object):

    def __init__(self, blockchainHelper, name):
        self.name = name
        self.version = 1
        self.withdrawalEpochLength = 10
        self.toAddress = "abcd"
        self.amount = Decimal("1.0")
        self.certificateVerificationKey = blockchainHelper.cert_utils.generate_params(self.name)
        self.cswVerificationKey = blockchainHelper.csw_utils.generate_params(self.name)
        self.constant = get_field_element_with_padding(generate_random_field_element(MODULUS_BITS), 1)
        self.customFieldsConfig = [MODULUS_BITS, MODULUS_BITS]
        self.bitvectorConfig = [[254*4, 151]]

    def to_rpc_args(self):
        return {
            "version": self.version,
            "withdrawalEpochLength": self.withdrawalEpochLength,
            "toaddress": self.toAddress,
            "amount":  self.amount,
            "wCertVk": self.certificateVerificationKey,
            "wCeasedVk": self.cswVerificationKey,
            "constant": self.constant,
            "vFieldElementCertificateFieldConfig": self.customFieldsConfig,
            "vBitVectorCertificateFieldConfig": self.bitvectorConfig
        }

class BlockchainHelper(object):

    def __init__(self, bitcoin_test_framework):
        self.sidechain_map = {}
        self.init_proof_keys(bitcoin_test_framework)
        self.nodes = bitcoin_test_framework.nodes

    def init_proof_keys(self, bitcoin_test_framework):
        self.cert_utils = CertTestUtils(bitcoin_test_framework.options.tmpdir, bitcoin_test_framework.options.srcdir)
        self.csw_utils = CSWTestUtils(bitcoin_test_framework.options.tmpdir, bitcoin_test_framework.options.srcdir)

    # sc_name is not the real sidechain id (it will be generated automatically during the call to sc_create)
    # but an identifier used to:
    # 1) search for the sidechain in the sidechain map
    # 2) generate the certificate and CSW verification keys
    def create_sidechain(self, sc_name, sidechain_version, should_fail = False):
        """
        Creates a sidechain with default parameters, only the name (used to
        store it locally in a map) and the version are required.
        """
        sc_input = SidechainCreationInput(self, sc_name)
        sc_input.version = sidechain_version

        return self.create_sidechain_from_args(sc_input, should_fail)

    def create_sidechain_from_args(self, sc_input, should_fail = False):
        """
        Creates a sidechain with the given custom parameters.
        """
        error_message = None

        # At the moment it's not possible to provide a custom bitvector configuration.
        # In the future, we should implement a function that given a custom configuration,
        # randomly generates a bitvector and computes its merkle root (for instance by
        # calling the MC_Test external tool).
        for bitvector_configuration in sc_input.bitvectorConfig:
            assert(bitvector_configuration == [254*4, 151])

        try:
            ret = self.nodes[0].sc_create(sc_input.to_rpc_args())
            assert(not should_fail)
            self.store_sidechain(sc_input, ret)
        except JSONRPCException as e:
            error_message = e.error['message']
            mark_logs(error_message, self.nodes, 1)
            assert(should_fail)

        return error_message

    def store_sidechain(self, sc_input, creation_response):
        """
        Stores a sidechain into a local map using the sidechain name as key.
        """
        self.sidechain_map[sc_input.name] = {
            "creation_args": sc_input,
            "creation_tx_id": creation_response["txid"],
            "sc_id": creation_response["scid"]
        }

    def get_sidechain_id(self, sc_name):
        """
        Returns the ID of the sidechain with the given name.
        """
        return self.sidechain_map[sc_name]["sc_id"]

    def send_certificate(self, sc_name, quality):
        """
        Sends a randomly generated certificate for the given sidechain (name).
        Only quality is required as a parameter.
        """
        sc_info = self.sidechain_map[sc_name]
        scid = sc_info["sc_id"]
        withdrawalEpochLength = sc_info["creation_args"].withdrawalEpochLength
        constant = sc_info["creation_args"].constant

        sc_creation_height = self.nodes[0].getscinfo(scid)['items'][0]['createdAtBlockHeight']
        current_height = self.nodes[0].getblockcount()
        epoch_number = (current_height - sc_creation_height + 1) // withdrawalEpochLength - 1
        end_epoch_block_hash = self.nodes[0].getblockhash(sc_creation_height - 1 + ((epoch_number + 1) * withdrawalEpochLength))
        epoch_cum_tree_hash = self.nodes[0].getblock(end_epoch_block_hash)['scCumTreeHash']
        scid_swapped = str(swap_bytes(scid))

        custom_fields = []
        bitvectors = [RANDOM_BITVECTOR_FIELD_ELEMENT] * len(sc_info["creation_args"].bitvectorConfig)

        for custom_field_config in sc_info["creation_args"].customFieldsConfig:
            # If the sidechain is version 0, due to a serialization and validation bug, only custom fields using at most 128 bits
            # are allowed. In this case, the custom fields configuration (used at creation time) must be 255 bits.
            if sc_info["creation_args"].version == 0:
                assert(custom_field_config == 255)
                custom_fields.append(get_field_element_with_padding(generate_random_field_element(128), 1))
            else:
                custom_fields.append(generate_random_field_element(custom_field_config))

        # The proof generation utility only accepts 32 bytes field elements, regardless of the configuration.
        custom_fields_with_padding = [get_field_element_with_padding(element, 1) for element in custom_fields]

        # Generate the certificate SNARK prook
        proof = self.cert_utils.create_test_proof(
            sc_name,
            scid_swapped,
            epoch_number,
            quality,
            0,
            0,
            epoch_cum_tree_hash,
            constant,
            [],
            [],
            custom_fields_with_padding + bitvectors)

        if proof is None:
            print("Unable to create proof")
            assert(False)

        # Send the certificate
        certificate_id = self.nodes[0].sc_send_certificate(
            scid,
            epoch_number,
            quality,
            epoch_cum_tree_hash,
            proof,
            [],
            0,
            0,
            -1,
            "",
            custom_fields,
            [RANDOM_BITVECTOR])

        return certificate_id
