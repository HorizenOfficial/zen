import subprocess
import os, os.path, binascii
import random
from subprocess import call

from test_framework.util import COIN


SC_FIELD_SIZE = 32
SC_FIELD_SAFE_SIZE = 31

# these should be aligned with the definitions in src/sc/sidechaintypes.h
MAX_SC_PROOF_SIZE_IN_BYTES = 9*1024                                                                     
MAX_SC_VK_SIZE_IN_BYTES    = 9*1024

def generate_random_field_element_hex():
    return os.urandom(SC_FIELD_SAFE_SIZE).hex() + "00" * (SC_FIELD_SIZE - SC_FIELD_SAFE_SIZE)

def generate_random_field_element_hex_list(len):
    return [generate_random_field_element_hex() for i in range(len)]

class MCTestUtils(object):

    def __init__(self, datadir, srcdir, ps_type = "cob_marlin"):
        self.datadir = datadir
        self.srcdir = srcdir
        assert(ps_type == "darlin" or ps_type == "cob_marlin")
        self.ps_type = ps_type

    def _get_file_prefix(self, circ_type, constant):
        return str(self.ps_type) + "_" + circ_type + ("_" if (constant != None and constant != False) else "_no_const_")

    def _get_args(self, op, circuit_type, constant, keyrot, ps_type, params_dir, num_constraints, segment_size, proof_path = None):
        args = [os.getenv("ZENDOOMC", os.path.join(self.srcdir, "zendoo/mcTest"))]

        args += [op, "-c", str(circuit_type), "-p", str(ps_type), "-s", str(segment_size), "-n", str(num_constraints)]

        if constant is not None and constant != False:
            args.append("-k")
            if constant == True:
                args.append("CONSTANT_PLACEHOLDER")
            else:
                args.append(str(constant))

        if keyrot == True:
            args.append("-r")

        args.append("--")   # Any parameter after the double dash is ignored by getopt, allowing to pass
                            # also negative values.

        args.append(str(params_dir))

        if proof_path is not None:
            args.append(str(proof_path))

        return args


    def _generate_params(self, id, circuit_type, constant, keyrot, ps_type, file_prefix, num_constraints, segment_size):
        params_dir = self._get_params_dir(id)
        if os.path.isfile(params_dir + file_prefix + "test_pk") and os.path.isfile(params_dir + file_prefix + "test_vk"):
            return self._get_vk(params_dir + file_prefix + "test_vk")

        args = self._get_args("generate", circuit_type, constant, keyrot, ps_type, params_dir, num_constraints, segment_size)
        subprocess.check_call(args)
        assert(os.path.isfile(params_dir + file_prefix + "test_pk"))
        return self._get_vk(params_dir + file_prefix + "test_vk")

    def _get_params_dir(self, id):
        target_dir = "{}/sc_{}_params/".format(self.datadir, id)
        if not os.path.isdir(target_dir):
            os.makedirs(target_dir)
        return target_dir

    def _get_proofs_dir(self, id):
        target_dir = "{}/sc_{}_proofs/".format(self.datadir, id)
        if not os.path.isdir(target_dir):
            os.makedirs(target_dir)
        return target_dir

    def _get_proof(self, proof_path):
        assert os.path.isfile(proof_path)
        proof = open(proof_path, "rb").read()
        return proof.hex()

    def _get_vk(self, vk_path):
        assert os.path.isfile(vk_path)
        vk = open(vk_path, "rb").read()
        return vk.hex()

class CertTestUtils(MCTestUtils):
    def __init__(self, datadir, srcdir, ps_type = "cob_marlin"):
        MCTestUtils.__init__(self, datadir, srcdir, ps_type)

    def generate_params(self, id, circ_type = "cert", constant = True, keyrot = False, num_constraints = 1 << 10, segment_size = 1 << 9):
        file_prefix = self._get_file_prefix(circ_type, constant)

        return self._generate_params(id, circ_type, constant, keyrot, self.ps_type, file_prefix, num_constraints, segment_size)

    def create_test_proof(self, id, scid, epoch_number, quality, btr_fee, ft_min_amount, end_cum_comm_tree_root, prev_cert_hash = None, constant = None, pks = [], amounts = [], custom_fields = [], num_constraints = 1 << 10, segment_size = 1 << 9):
        circ_type = "cert"

        file_prefix = self._get_file_prefix(circ_type, constant)
        params_dir = self._get_params_dir(id)
        if not os.path.isfile(params_dir + file_prefix + "test_pk") or not os.path.isfile(params_dir + file_prefix + "test_vk"):
            return

        proof_path = "{}_epoch_{}_{}_proof".format(self._get_proofs_dir(id), epoch_number, file_prefix)
        args = self._get_args("create", circ_type, constant, prev_cert_hash, self.ps_type, params_dir, num_constraints, segment_size, proof_path)

        if prev_cert_hash is None:
            prev_cert_hash = "NO_PREV_CERT_HASH"
        elif prev_cert_hash == 0:
            prev_cert_hash = "PHANTOM_PREV_CERT_HASH"

        args += [str(scid), str(end_cum_comm_tree_root), str(prev_cert_hash), str(epoch_number), str(quality), str(int(round(btr_fee * COIN))), str(int(round(ft_min_amount * COIN)))]
        args.append(str(len(pks)))
        for (pk, amount) in zip(pks, amounts):
            args.append(str(pk))
            args.append(str(int(round(amount * COIN)))) #codebase works in satoshi
        args.append(str(len(custom_fields)))
        for custom_field in custom_fields:
            args.append(str(custom_field))
        subprocess.check_call(args)
        return self._get_proof(proof_path)

class CSWTestUtils(MCTestUtils):
    def __init__(self, datadir, srcdir, ps_type = "cob_marlin"):
        MCTestUtils.__init__(self, datadir, srcdir, ps_type)

    def generate_params(self, id, circ_type = "csw", constant = True, keyrot = False, num_constraints = 1 << 10, segment_size = 1 << 9):
        file_prefix = self._get_file_prefix(circ_type, constant)

        return self._generate_params(id, circ_type, constant, keyrot, self.ps_type, file_prefix, num_constraints, segment_size)

    def create_test_proof(self, id, amount, sc_id, nullifier, mc_pk_hash, end_cum_comm_tree_root,
        cert_data_hash = None, constant = None, num_constraints = 1 << 10, segment_size = 1 << 9):
        circ_type = "csw"

        file_prefix = self._get_file_prefix(circ_type, constant)
        params_dir = self._get_params_dir(id)
        if not os.path.isfile(params_dir + file_prefix + "test_pk") or not os.path.isfile(params_dir + file_prefix + "test_vk"):
            return

        proof_path = "{}_addr_{}_{}_proof".format(self._get_proofs_dir(id), mc_pk_hash, file_prefix)
        args = self._get_args("create", circ_type, constant, None, self.ps_type, params_dir, num_constraints, segment_size, proof_path)

        if cert_data_hash is None:
            cert_data_hash = "NO_CERT_DATA_HASH"
        else:
            cert_data_hash = str(cert_data_hash)

        args += [str(sc_id), str(end_cum_comm_tree_root), str(cert_data_hash), str(int(round(amount * COIN))),  str(nullifier), str(mc_pk_hash)]

        subprocess.check_call(args)
        return self._get_proof(proof_path)
