import subprocess
import os, os.path, binascii
import random
from subprocess import call

SC_FIELD_SIZE = 32
SC_FIELD_SAFE_SIZE = 31
MAX_SC_PROOF_SIZE_IN_BYTES = 1024*10                                                                     
MAX_SC_VK_SIZE_IN_BYTES    = 1024*10
COIN = 100000000

def generate_random_field_element_hex():
    return (binascii.b2a_hex(os.urandom(SC_FIELD_SAFE_SIZE)) + "00" * (SC_FIELD_SIZE - SC_FIELD_SAFE_SIZE))

def generate_random_field_element_hex_list(len):
    return [generate_random_field_element_hex() for i in xrange(len)]

class MCTestUtils(object):

    def __init__(self, datadir, srcdir, ps_type = "cob_marlin"):
        self.datadir = datadir
        self.srcdir = srcdir
        assert(ps_type == "darlin" or ps_type == "cob_marlin")
        self.ps_type = ps_type

    def _generate_params(self, id, circuit_type, ps_type, file_prefix):
        params_dir = self._get_params_dir(id)

        if os.path.isfile(params_dir + file_prefix + "test_pk") and os.path.isfile(params_dir + file_prefix + "test_vk"):
            return
        args = []
        args.append(os.getenv("ZENDOOMC", os.path.join(self.srcdir, "zendoo/mcTest")))
        args.append("generate")
        args.append(str(circuit_type))
        args.append(str(ps_type))
        args.append(str(params_dir))

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
        return binascii.b2a_hex(proof)

    def _get_vk(self, vk_path):
        assert os.path.isfile(vk_path)
        vk = open(vk_path, "rb").read()
        return binascii.b2a_hex(vk)

class CertTestUtils(MCTestUtils):
    def __init__(self, datadir, srcdir, ps_type = "cob_marlin"):
        MCTestUtils.__init__(self, datadir, srcdir, ps_type)
        self.file_prefix = str(ps_type) + "_cert_"

    def generate_params(self, id):
        return self._generate_params(id, "cert", self.ps_type, self.file_prefix)

    def create_test_proof(self, id, epoch_number, quality, btr_fee, ft_min_amount, constant, end_cum_comm_tree_root, pks = [], amounts = [], custom_fields = []):
        params_dir = self._get_params_dir(id)
        if not os.path.isfile(params_dir + self.file_prefix + "test_pk") or not os.path.isfile(params_dir + self.file_prefix + "test_vk"):
            return
        proof_path = "{}_epoch_{}_{}_proof".format(self._get_proofs_dir(id), epoch_number, self.file_prefix)
        args = []
        args.append(os.getenv("ZENDOOMC", os.path.join(self.srcdir, "zendoo/mcTest")))
        args += ["create", "cert", str(self.ps_type), str(proof_path), str(params_dir)]
        args += [str(epoch_number), str(quality), str(constant), str(end_cum_comm_tree_root), str(int(btr_fee * COIN)), str(int(ft_min_amount * COIN))]
        args.append(str(len(pks)))
        for (pk, amount) in zip(pks, amounts):
            args.append(str(pk))
            args.append(str(int(amount * COIN))) #codebase works in satoshi
        args.append(str(len(custom_fields)))
        for custom_field in custom_fields:
            args.append(str(custom_field))
        subprocess.check_call(args)
        return self._get_proof(proof_path)

class CSWTestUtils(MCTestUtils):
    def __init__(self, datadir, srcdir, ps_type = "cob_marlin"):
        MCTestUtils.__init__(self, datadir, srcdir, ps_type)
        self.file_prefix = str(ps_type) + "_csw_"

    def generate_params(self, id):
        return self._generate_params(id, "csw", self.ps_type, self.file_prefix)

    def create_test_proof(self, id, amount, sc_id, nullifier, mc_pk_hash, end_cum_comm_tree_root, cert_data_hash):
        params_dir = self._get_params_dir(id)
        if not os.path.isfile(params_dir + self.file_prefix + "test_pk") or not os.path.isfile(params_dir + self.file_prefix + "test_vk"):
            return
        proof_path = "{}_addr_{}_{}_proof".format(self._get_proofs_dir(id), mc_pk_hash, self.file_prefix)
        args = []
        args.append(os.getenv("ZENDOOMC", os.path.join(self.srcdir, "zendoo/mcTest")))
        args += ["create", "csw", str(self.ps_type), str(proof_path), str(params_dir)]
        args += [str(int(amount * COIN)), str(sc_id), str(nullifier), str(mc_pk_hash), str(end_cum_comm_tree_root), str(cert_data_hash)]
        subprocess.check_call(args)
        return self._get_proof(proof_path)
