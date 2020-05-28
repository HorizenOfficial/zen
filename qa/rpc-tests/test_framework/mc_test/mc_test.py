import subprocess
import os, binascii
import random

SC_FIELD_SIZE = 96
SC_FIELD_SAFE_SIZE = 94
SC_PROOF_SIZE = 771
SC_VK_SIZE = 1544

def generate_vk():
    args = ["./mcTest", "generate"]
    subprocess.check_call(args)

def create_test_proof(end_epoch_block_hash, prev_end_epoch_block_hash, quality, constant, pks, amounts):
    args = ["./mcTest"]
    args += [str(end_epoch_block_hash), str(prev_end_epoch_block_hash), str(quality), str(constant)]
    for (pk, amount) in zip(pks, amounts):
        args.append(str(pk))
        args.append(str(amount))
    subprocess.check_call(args)

def generate_random_field_element_hex():
    return (binascii.b2a_hex(os.urandom(SC_FIELD_SAFE_SIZE)) + "00" * (SC_FIELD_SIZE - SC_FIELD_SAFE_SIZE))

def get_proof():
    assert os.path.exists("./test_mc_proof")
    proof = open("./test_mc_proof", "rb").read()
    assert (len(proof) == SC_PROOF_SIZE)
    return proof

def get_vk():
    assert os.path.exists("./test_mc_vk")
    vk = open("./test_mc_vk", "rb").read()
    assert (len(vk) == SC_VK_SIZE)
    return vk