# blocktools.py - utilities for manipulating blocks and transactions
#
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#

from mininode import CBlock, CTransaction, CTxIn, CTxOut, COutPoint, ToHex
from script import CScript, OP_0, OP_EQUAL, OP_HASH160, OP_DUP, OP_CHECKBLOCKATHEIGHT, OP_EQUALVERIFY, OP_CHECKSIG
from decimal import Decimal
from cStringIO import StringIO
from binascii import unhexlify, hexlify
from util import hex_str_to_bytes, swap_bytes

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
        block.nBits = 0x200f0f0f # Will break after a difficulty adjustment...
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

counter=1
# Create an anyone-can-spend coinbase transaction, assuming no miner fees
def create_coinbase(heightAdjust = 0):
    global counter
    coinbase = CTransaction()
    coinbase.vin.append(CTxIn(COutPoint(0, 0xffffffff), 
                CScript([counter+heightAdjust, OP_0]), 0xffffffff))
    counter += 1
    coinbaseoutput = CTxOut()
    coinbaseoutput.nValue = int(12.5*100000000)
    halvings = int((counter+heightAdjust)/150) # regtest
    coinbaseoutput.nValue >>= halvings
    coinbaseoutput.scriptPubKey = ""
    coinbase.vout = [ coinbaseoutput ]
    if halvings == 0: # regtest
        froutput = CTxOut()
        froutput.nValue = coinbaseoutput.nValue / 5
        # regtest
        fraddr = bytearray([0x67, 0x08, 0xe6, 0x67, 0x0d, 0xb0, 0xb9, 0x50,
                            0xda, 0xc6, 0x80, 0x31, 0x02, 0x5c, 0xc5, 0xb6,
                            0x32, 0x13, 0xa4, 0x91])
        froutput.scriptPubKey = CScript([OP_HASH160, fraddr, OP_EQUAL])
        coinbaseoutput.nValue -= froutput.nValue
        coinbase.vout = [ coinbaseoutput, froutput ]
    coinbase.calc_sha256()
    return coinbase

# Create a transaction with an anyone-can-spend output, that spends the
# nth output of prevtx.
def create_transaction(prevtx, n, sig, value):
    tx = CTransaction()
    assert(n < len(prevtx.vout))
    tx.vin.append(CTxIn(COutPoint(prevtx.sha256, n), sig, 0xffffffff))
    tx.vout.append(CTxOut(value, ""))
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
    f = StringIO(unhexlify(rawTx))
    tx_01.deserialize(f)

    # corrupt vouts in this Tx
    for vout_idx in range(len(tx_01.vout)):
        decodedScriptOrig = node_from.decodescript(hexlify(tx_01.vout[vout_idx].scriptPubKey))

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

