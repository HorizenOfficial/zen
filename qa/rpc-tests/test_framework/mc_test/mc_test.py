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

    def _generate_params(self, id, circuit_type, ps_type, file_prefix, num_constraints, segment_size):
        params_dir = self._get_params_dir(id)

        if os.path.isfile(params_dir + file_prefix + "test_pk") and os.path.isfile(params_dir + file_prefix + "test_vk"):
            return
        args = []
        args.append(os.getenv("ZENDOOMC", os.path.join(self.srcdir, "zendoo/mcTest")))
        args.append("generate")
        args.append(str(circuit_type))
        args.append(str(ps_type))
        args.append(str(params_dir))
        args.append(str(segment_size))
        args.append(str(num_constraints))

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

    def _get_file_prefix(self, circ_type):
        return str(self.ps_type) + "_" + circ_type + "_"

    def generate_params(self, id, circ_type = "cert", num_constraints = 1 << 10, segment_size = 1 << 9):
        file_prefix = self._get_file_prefix(circ_type)

        return self._generate_params(id, circ_type, self.ps_type, file_prefix, num_constraints, segment_size)

    def create_test_proof(self, id, scid, epoch_number, quality, btr_fee, ft_min_amount, end_cum_comm_tree_root, constant = None, pks = [], amounts = [], custom_fields = [], num_constraints = 1 << 10, segment_size = 1 << 9):
        if constant is not None:
            circ_type = "cert"
        else:
            circ_type = "cert_no_const"

        file_prefix = self._get_file_prefix(circ_type)
        params_dir = self._get_params_dir(id)
        if not os.path.isfile(params_dir + file_prefix + "test_pk") or not os.path.isfile(params_dir + file_prefix + "test_vk"):
            return
        proof_path = "{}_epoch_{}_{}_proof".format(self._get_proofs_dir(id), epoch_number, file_prefix)
        args = []
        args.append(os.getenv("ZENDOOMC", os.path.join(self.srcdir, "zendoo/mcTest")))
        args += ["create", str(circ_type), str(self.ps_type), str(proof_path), str(params_dir), str(segment_size)]
        args += [str(scid), str(epoch_number), str(quality)]
        if constant is not None:
             args.append(str(constant))

        args += [str(end_cum_comm_tree_root), str(int(round(btr_fee * COIN))), str(int(round(ft_min_amount * COIN)))]
        args.append(str(num_constraints))
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

    def _get_file_prefix(self, circ_type):
        return str(self.ps_type) + "_" + circ_type + "_"

    def generate_params(self, id, circ_type = "csw", num_constraints = 1 << 10, segment_size = 1 << 9):
        file_prefix = self._get_file_prefix(circ_type)

        return self._generate_params(id, circ_type, self.ps_type, file_prefix, num_constraints, segment_size)

    def create_test_proof(self, id, amount, sc_id, nullifier, mc_pk_hash, end_cum_comm_tree_root,
        cert_data_hash = None, constant = None, num_constraints = 1 << 10, segment_size = 1 << 9):
        if constant is not None:
            circ_type = "csw"
        else:
            circ_type = "csw_no_const"

        file_prefix = self._get_file_prefix(circ_type)
        params_dir = self._get_params_dir(id)
        if not os.path.isfile(params_dir + file_prefix + "test_pk") or not os.path.isfile(params_dir + file_prefix + "test_vk"):
            return
        proof_path = "{}_addr_{}_{}_proof".format(self._get_proofs_dir(id), mc_pk_hash, file_prefix)
        args = []
        args.append(os.getenv("ZENDOOMC", os.path.join(self.srcdir, "zendoo/mcTest")))

        args += ["create", str(circ_type), str(self.ps_type), str(proof_path), str(params_dir), str(segment_size)]
        args += [str(int(round(amount * COIN))), str(sc_id), str(nullifier), str(mc_pk_hash), str(end_cum_comm_tree_root), str(num_constraints)]

        if cert_data_hash is not None:
            args.append(str(cert_data_hash))
        else:
            args.append(str("NO_CERT_DATA_HASH"))

        if constant is not None:
             args.append(str(constant))

        subprocess.check_call(args)
        return self._get_proof(proof_path)
