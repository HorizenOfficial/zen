# blocktools.py - utilities for manipulating blocks and transactions
#
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#

from test_framework.mininode import CBlock, CTransaction, CTxIn, CTxOut, COutPoint, ToHex
from test_framework.script import OP_TRUE, CScript, OP_0, OP_EQUAL, OP_HASH160, OP_DUP, OP_CHECKBLOCKATHEIGHT, OP_EQUALVERIFY, OP_CHECKSIG
from decimal import Decimal
from io import BytesIO
from binascii import unhexlify, hexlify
from test_framework.util import bytes_to_hex_str, hex_str_to_bytes, hex_str_to_str, swap_bytes, to_satoshis

# Create a block (with regtest difficulty)
def create_block(hashprev, coinbase, nTime=None, nBits=None):
    block = CBlock()
    if nTime is None:
        import time
        block.nTime = int(time.time()+600)
    else:
        block.nTime = nTime
    block.hashPrevBlock = hashprev
    if nBits is None:
        block.nBits = 0x207f0f0f # Will break after a difficulty adjustment...
    else:
        block.nBits = nBits
    block.vtx.append(coinbase)
    block.hashMerkleRoot = block.calc_merkle_root()
    block.calc_sha256()
    return block

def serialize_script_num(value):
    r = bytearray(0)
    if value == 0:
        return r
    neg = value < 0
    absvalue = -value if neg else value
    while (absvalue):
        r.append(chr(absvalue & 0xff))
        absvalue >>= 8
    if r[-1] & 0x80:
        r.append(0x80 if neg else 0)
    elif neg:
        r[-1] |= 0x80
    return r


def get_nBits(chainHeight):
    # Hard-coding max target would not work due to rounding behaviour of arith_uint256 type in division in the
    # pow.cpp src code. This is an approximation for regtest.
    #---
    # consensus.nPowAveragingWindow = 2
    # genesis.nBits = 0x207f0f0f
    # nBits stabilizes after 0x34 windows (based on experiments/observations)
    nBits = 0x207f0f0f
    r = min(chainHeight // 2, 52)
    nBits -= r
    #print "--- called with {} --> nBits = {}".format(chainHeight, nBits)
    return nBits


fork_points = {}

# shares (in thousandths) of coinbase subsidy for foundation, super nodes and secure nodes
# the name is the one of relevant forks in the zend src code 
# ---
fork1_chainsplitfork            = {"foundation":  85, "sup_nodes":   0, "sec_nodes":   0}
fork3_communityfundandrpfixfork = {"foundation": 120, "sup_nodes":   0, "sec_nodes":   0} 
fork4_nulltransactionfork       = {"foundation": 100, "sup_nodes": 100, "sec_nodes": 100} 
fork5_shieldfork                = {"foundation": 200, "sup_nodes": 100, "sec_nodes": 100} 

fork_points[  0] = fork1_chainsplitfork
fork_points[101] = fork3_communityfundandrpfixfork
fork_points[105] = fork4_nulltransactionfork
fork_points[200] = fork5_shieldfork

# hashes of the redeemscript for the p2sh addresses, as of now they do not change across forks
# ---
found_addr = bytearray([
    0xea, 0x81, 0xee, 0x2d, 0x87, 0x7a, 0x25, 0xc7,
    0x53, 0x0a, 0x33, 0xfc, 0xf5, 0xa6, 0x5c, 0x72,
    0xf6, 0x81, 0x25, 0x0f])

supn_addr = bytearray([
    0xe7, 0xd2, 0x5d, 0x82, 0xbe, 0x23, 0x1c, 0xf7,
    0x7a, 0xb8, 0xae, 0xcb, 0x80, 0xb6, 0x06, 0x69,
    0x23, 0x81, 0x9f, 0xfc])

secn_addr = bytearray([
    0xca, 0x76, 0xbe, 0xb2, 0x5c, 0x5f, 0x1c, 0x29,
    0xc3, 0x05, 0xa2, 0xb3, 0xe7, 0x1a, 0x2d, 0xe5,
    0xfe, 0x1d, 0x2e, 0xed])

key_list = sorted(fork_points.keys())

def get_coinbase_quotas(fork_height):
    found = False
    for k in range(1, len(key_list)):
        if fork_height < key_list[k]:
            found = True
            key = key_list[k-1] 
            break
    if not found: 
        key = key_list[-1] 
    return fork_points[key]["foundation"], fork_points[key]["sup_nodes"], fork_points[key]["sec_nodes"]


# Create an anyone-can-spend coinbase transaction, assuming no miner fees given a height
def create_coinbase_h(block_height, halving_interval=2000, heightAdjust = 0):

    subsidy = int(12.5*100000000)

    halvings = int((block_height+heightAdjust)// halving_interval) # 2000 is default for regtest
    subsidy >>= halvings

    coinbase = CTransaction()
    coinbase.vin.append(CTxIn(COutPoint(0, 0xffffffff), 
                CScript([block_height+heightAdjust, OP_0]), 0xffffffff))

    coinbaseoutput = CTxOut()
    coinbaseoutput.nValue = subsidy

    coinbaseoutput.scriptPubKey = b""
    coinbase.vout.append(coinbaseoutput)

    found, supn, secn = get_coinbase_quotas(block_height)

    found_out = CTxOut()
    found_out.nValue = subsidy * found // 1000
    found_out.scriptPubKey = CScript([OP_HASH160, found_addr, OP_EQUAL])
    coinbaseoutput.nValue -= found_out.nValue
    coinbase.vout.append(found_out)

    if supn > 0:
        supn_out = CTxOut()
        supn_out.nValue = subsidy * supn // 1000
        supn_out.scriptPubKey = CScript([OP_HASH160, supn_addr, OP_EQUAL])
        coinbaseoutput.nValue -= supn_out.nValue
        coinbase.vout.append(supn_out)

    if secn > 0:
        secn_out = CTxOut()
        secn_out.nValue = subsidy * secn // 1000
        secn_out.scriptPubKey = CScript([OP_HASH160, secn_addr, OP_EQUAL])
        coinbaseoutput.nValue -= secn_out.nValue
        coinbase.vout.append(secn_out)

    coinbase.calc_sha256()
    return coinbase




counter=1
# Create an anyone-can-spend coinbase transaction, assuming no miner fees
def create_coinbase(heightAdjust = 0, comm_quota=85):
    global counter
    coinbase = CTransaction()
    coinbase.vin.append(CTxIn(COutPoint(0, 0xffffffff), 
                CScript([counter+heightAdjust, OP_0]), 0xffffffff))
    counter += 1
    coinbaseoutput = CTxOut()
    coinbaseoutput.nValue = to_satoshis(12.5)
    halvings = int((counter+heightAdjust)/2000) # regtest
    coinbaseoutput.nValue >>= halvings
    coinbaseoutput.scriptPubKey = CScript([OP_TRUE])
    coinbase.vout = [ coinbaseoutput ]
    if halvings == 0: # regtest
        comm_output = CTxOut()
        comm_output.nValue = (coinbaseoutput.nValue * comm_quota // 1000) 
        # regtest
        com_addr = bytearray([0xb6, 0x86, 0x3b, 0x18, 0x2a, 0x52, 0x74, 0x5b,
                              0xf6, 0xd5, 0xfb, 0x19, 0x01, 0x39, 0xa2, 0xaa,
                              0x87, 0x6c, 0x08, 0xf5])
        comm_output.scriptPubKey = CScript([OP_HASH160, com_addr, OP_EQUAL])
        coinbaseoutput.nValue -= comm_output.nValue
        coinbase.vout.append(comm_output)
    coinbase.calc_sha256()
    return coinbase

# Create a transaction with an anyone-can-spend output, that spends the
# nth output of prevtx.
def create_transaction(prevtx, n, sig, value):
    tx = CTransaction()
    assert(n < len(prevtx.vout))
    tx.vin.append(CTxIn(COutPoint(prevtx.sha256, n), sig, 0xffffffff))
    tx.vout.append(CTxOut(value, b""))
    tx.calc_sha256()
    return tx

# create a signed tx with a tampered replay protection script according to a mode parameter
MODE_HEIGHT    = 0 
MODE_SWAP_ARGS = 1
MODE_NON_MIN_ENC = 2
def create_tampered_rawtx_cbh(node_from, node_to, tx_amount, fee, mode):

    genesis_block_hash = node_from.getblock(str(0))['hash']

    # select necessary UTXOs
    usp = node_from.listunspent()
    assert(len(usp) != 0)

    amount = Decimal('0')
    inputs = []

    for x in usp:
        amount += Decimal(x['amount'])
        inputs.append( {"txid":x['txid'], "vout":x['vout']})
        if amount >= tx_amount+fee:
            break

    outputs = {node_from.getnewaddress(): (Decimal(amount) - tx_amount - fee), node_to.getnewaddress(): tx_amount}
    rawTx = node_from.createrawtransaction(inputs, outputs)

    # build an object from the raw Tx in order to be able to modify it
    tx_01 = CTransaction()
    f = BytesIO(unhexlify(rawTx))
    tx_01.deserialize(f)

    # corrupt vouts in this Tx
    for vout_idx in range(len(tx_01.vout)):
        decodedScriptOrig = node_from.decodescript(bytes_to_hex_str(tx_01.vout[vout_idx].scriptPubKey))

        scriptOrigAsm = decodedScriptOrig['asm']
        params = scriptOrigAsm.split()
        hash160         = hex_str_to_bytes(params[2])
        original_height = int(params[6])
        original_hash   = hex_str_to_bytes(params[5])

        if mode == MODE_HEIGHT:
            # new referenced block height
            evil_height = -1
            # new referenced block hash
            modTargetHash = hex_str_to_bytes(swap_bytes(genesis_block_hash))
            # build modified script: CScript is putting a 4f (OP_NEGATE) for our -1
            # edit script.py to send different stuff for the -1 value (like ff itself!)
            modScriptPubKey = CScript([OP_DUP, OP_HASH160, hash160, OP_EQUALVERIFY, OP_CHECKSIG, modTargetHash, evil_height, OP_CHECKBLOCKATHEIGHT])
        elif mode == MODE_SWAP_ARGS:
            modScriptPubKey = CScript([OP_DUP, OP_HASH160, hash160, OP_EQUALVERIFY, OP_CHECKSIG, original_height, original_hash, OP_CHECKBLOCKATHEIGHT])
        elif mode == MODE_NON_MIN_ENC:
            non_min_h =  hex_str_to_bytes("07000000")
            modScriptPubKey = CScript([OP_DUP, OP_HASH160, hash160, OP_EQUALVERIFY, OP_CHECKSIG, original_hash, non_min_h, OP_CHECKBLOCKATHEIGHT])
        else:
            assert(False)

        tx_01.vout[vout_idx].scriptPubKey = modScriptPubKey

    tx_01.rehash()
    signedRawTx = node_from.signrawtransaction(ToHex(tx_01))
    return signedRawTx

