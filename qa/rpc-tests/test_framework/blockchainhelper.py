#!/usr/bin/env python3

import random

from decimal import Decimal

from test_framework.authproxy import JSONRPCException
from test_framework.mc_test.mc_test import CertTestUtils, CSWTestUtils
from test_framework.util import get_field_element_with_padding, mark_logs, swap_bytes, get_epoch_data

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

"""
Dictionary of default parameters that can be used when calling 'create_sidechain'.
"""
SidechainParameters = {
    "DEFAULT_SC_V0":                { "version": 0 },
    "DEFAULT_SC_V1":                { "version": 1 },
    "DEFAULT_SC_V2_CEASABLE":       { "version": 2, "withdrawalEpochLength": 10 },
    "DEFAULT_SC_V2_NON_CEASABLE":   { "version": 2, "withdrawalEpochLength": 0 }
}

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

    # Calculate the number of bytes needed to store the requested bits
    bytes_len = bits_len // 8

    if bits_len % 8 != 0:
        bytes_len += 1

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
    field_element_hex = field_element_hex[2:]

    # The field element hex string must contain exactly bytes_len * 2 characters
    field_element_hex = field_element_hex.rjust(bytes_len * 2, "0")

    # Convert to little endian for usage in the blockchain
    field_element_hex = "".join(reversed([field_element_hex[i:i+2] for i in range(0, len(field_element_hex), 2)]))

    return field_element_hex

class SidechainCreationInput:

    def __init__(self, blockchainHelper, name, version):
        self.name = name
        self.version = version
        self.withdrawalEpochLength = 10
        self.toAddress = "abcd"
        self.amount = Decimal("1.0")
        self.certificateVerificationKey = blockchainHelper.cert_utils.generate_params(self.name, keyrot = False if version < 2 else True)
        self.cswVerificationKey = blockchainHelper.csw_utils.generate_params(self.name)
        self.constant = get_field_element_with_padding(generate_random_field_element(MODULUS_BITS), 1)
        self.customFieldsConfig = [MODULUS_BITS, MODULUS_BITS]
        self.bitvectorConfig = [[254*4, 151]]

    def is_non_ceasable(self) -> bool:
        return self.version == 2 and self.withdrawalEpochLength == 0

    @staticmethod
    def from_rpc_args(blockchainHelper, name, args):

        # "Version" is a mandatory field
        assert("version" in args)

        # Check that optional_parameters contains only valid keys
        for key in args:
            if key not in [
                "version",
                "withdrawalEpochLength",
                "toAddress",
                "amount",
                "wCertVk",
                "wCeasedVk",
                "constant",
                "vFieldElementCertificateFieldConfig",
                "vBitVectorCertificateFieldConfig"
            ]:
                raise JSONRPCException("Invalid key in args: " + key)

        creation_input = SidechainCreationInput(blockchainHelper, name, args["version"])

        if "withdrawalEpochLength" in args:
            creation_input.withdrawalEpochLength = args["withdrawalEpochLength"]

        if "toAddress" in args:
            creation_input.toAddress = args["toAddress"]

        if "amount" in args:
            creation_input.amount = Decimal(args["amount"])

        if "wCertVk" in args:
            creation_input.certificateVerificationKey = args["wCertVk"]

        if "wCeasedVk" in args:
            creation_input.cswVerificationKey = args["wCeasedVk"]
        elif creation_input.is_non_ceasable():
            # Setting the CSW verification key is not allowed for non-ceasable sidechains
            creation_input.cswVerificationKey = ""

        if "constant" in args:
            creation_input.constant = args["constant"]

        if "vFieldElementCertificateFieldConfig" in args:
            creation_input.customFieldsConfig = args["vFieldElementCertificateFieldConfig"]

        if "vBitVectorCertificateFieldConfig" in args:
            creation_input.bitvectorConfig = args["vBitVectorCertificateFieldConfig"]

        return creation_input

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

class BlockchainHelper:

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
    def create_sidechain(self, sc_name, creation_arguments, should_fail = False):
        """
        Creates a sidechain with the given name
        (used to store it locally in a map) and
        the creation arguments provided.
        Note that among these arguments, only
        "version" is mandatory.
        """

        # "version" is a mandatory argument
        assert("version" in creation_arguments)

        sc_input = SidechainCreationInput.from_rpc_args(self, sc_name, creation_arguments)

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

    def store_sidechain(self, sc_input: SidechainCreationInput, creation_response):
        """
        Stores a sidechain into a local map using the sidechain name as key.
        """
        self.sidechain_map[sc_input.name] = {
            "creation_args": sc_input,
            "creation_tx_id": creation_response["txid"],
            "last_certificate_epoch": -1,
            "sc_id": creation_response["scid"]
        }

        # For non-ceasable sidechains it is useful to store the last referenced height.
        # Given that we are storing the sidechain before the related transaction is mined,
        # the last referenced height is the current_height + 1.
        if sc_input.is_non_ceasable():
            self.sidechain_map[sc_input.name]["last_referenced_height"] = self.nodes[0].getblockcount() + 1

    def get_sidechain_id(self, sc_name):
        """
        Returns the ID of the sidechain with the given name.
        """
        return self.sidechain_map[sc_name]["sc_id"]

    def send_certificate(self, sc_name, quality, referenced_height = None):
        """
        Sends a randomly generated certificate for the given sidechain (name).
        Only quality is required as a parameter.
        referenced_height is optional and is used for non-ceasable sidechains only
        """
        sc_info = self.sidechain_map[sc_name]
        scid = sc_info["sc_id"]
        withdrawalEpochLength = sc_info["creation_args"].withdrawalEpochLength
        constant = sc_info["creation_args"].constant

        if (referenced_height is None):
            referenced_height = self.nodes[0].getblockcount()

        # If this is a ceasing sidechain, compute the epoch, otherwise it's the next after the last one
        version = sc_info["creation_args"].version
        is_non_ceasing_sidechain = version == 2 and sc_info["creation_args"].withdrawalEpochLength == 0

        epoch_number, epoch_cum_tree_hash, prev_cert_hash = get_epoch_data(scid, self.nodes[0], withdrawalEpochLength, is_non_ceasing_sidechain, referenced_height)

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
            prev_cert_hash = prev_cert_hash if version >= 2 else None,
            constant = constant,
            pks = [],
            amounts = [],
            custom_fields = custom_fields_with_padding + bitvectors)

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

        sc_info["last_certificate_epoch"] = epoch_number

        if is_non_ceasing_sidechain:
            sc_info["last_referenced_height"] = referenced_height

        return certificate_id
