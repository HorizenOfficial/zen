#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_true, assert_false, assert_equal, mark_logs
from test_framework.mininode import COIN, hash256, ser_string

from binascii import a2b_hex, b2a_hex
from hashlib import sha256
from decimal import Decimal
from struct import unpack, pack

import binascii
import codecs

# this is defined in src/primitives/block.h
SC_CERTIFICATE_BLOCK_VERSION = 3

# this is defined in src/primitives/block.cpp
SC_NULL_HASH = hash256(ser_string("Horizen ScTxsCommitment null hash string"))

SC_EPOCH_LENGTH = 5
SC_CREATION_AMOUNT = Decimal("1.0")
SC_CERT_AMOUNT     = Decimal("0.5")

DEBUG_MODE = 1

NODE_LIST = []

def check_array_result(object_array, to_match, expected):
    """
    Pass in array of JSON objects, a dictionary with key/value pairs
    to match against, and another dictionary with expected key/value
    pairs.
    """
    num_matched = 0
    for item in object_array:
        all_match = True
        for key,value in to_match.items():
            if item[key] != value:
                all_match = False
        if not all_match:
            continue
        for key,value in expected.items():
            if item[key] != value:
                raise AssertionError("%s : expected %s=%s"%(str(item), str(key), str(value)))
            num_matched = num_matched+1
    if num_matched == 0:
        raise AssertionError("No objects matched %s"%(str(to_match)))

def b2x(b):
    return b2a_hex(b).decode('ascii')

# NOTE: This does not work for signed numbers (set the high bit) or zero (use b'\0')
def encodeUNum(n):
    s = bytearray(b'\1')
    while n > 127:
        s[0] += 1
        s.append(n % 256)
        n //= 256
    s.append(n)
    return bytes(s)

def varlenEncode(n):
    if n < 0xfd:
        return pack('<B', n)
    if n <= 0xffff:
        return b'\xfd' + pack('<H', n)
    if n <= 0xffffffff:
        return b'\xfe' + pack('<L', n)
    return b'\xff' + pack('<Q', n)

def dblsha(b):
    return sha256(sha256(b).digest()).digest()

def genmrklroot(leaflist):
    cur = leaflist
    while len(cur) > 1:
        n = []
        if len(cur) & 1:
            cur.append(cur[-1])
        for i in range(0, len(cur), 2):
            n.append(dblsha(cur[i] + cur[i+1]))
        cur = n
    return cur[0]

def swap_bytes(hex_string):
    return codecs.encode(codecs.decode(hex_string, 'hex')[::-1], 'hex').decode()

def template_to_bytes(tmpl, txlist, certlist, input_sc_commitment = None):
    blkver = pack('<L', tmpl['version'])
    objlist = txlist + certlist
    mrklroot = genmrklroot(list(dblsha(a) for a in objlist))
    sc_commitment = b'\0'*32
    if input_sc_commitment != None:
        sc_commitment = input_sc_commitment
        mark_logs(("computed sc_commitment: %s" % swap_bytes(binascii.hexlify(sc_commitment))), NODE_LIST, DEBUG_MODE)
    timestamp = pack('<L', tmpl['curtime'])
    nonce = b'\0'*32
    soln = b'\0'
    blk = blkver + a2b_hex(tmpl['previousblockhash'])[::-1] + mrklroot + sc_commitment + timestamp + a2b_hex(tmpl['bits'])[::-1] + nonce + soln
    blk += varlenEncode(len(txlist))
    for tx in txlist:
        blk += tx
    if tmpl['version'] == SC_CERTIFICATE_BLOCK_VERSION:
        # fill vector of certificates from this version on 
        blk += varlenEncode(len(certlist))
        for cert in certlist:
            blk += cert
    return blk

def template_to_hex(tmpl, txlist, certlist, input_sc_commitment):
    return b2x(template_to_bytes(tmpl, txlist, certlist, input_sc_commitment))

def assert_template(node, tmpl, txlist, certlist, expect, input_sc_commitment = None):
#    try:
#        print "list=",template_to_hex(tmpl, txlist)
#        raw_input("Pres to continue 1...")
     rsp = node.getblocktemplate({'data':template_to_hex(tmpl, txlist, certlist, input_sc_commitment),'mode':'proposal'})
     if rsp != expect:
         print "rsp: ", rsp
         raise AssertionError('unexpected: %s' % (rsp,))
#    except JSONRPCException as e:
#            print "exception: ", e.error['message']

class GetBlockTemplateProposalTest(BitcoinTestFramework):
    '''
    Test block proposals with getblocktemplate.
    '''

    def run_test(self):
        NODE_LIST = self.nodes

        self.nodes[0].generate(1) # Mine a block to leave initial block download
        self.sync_all()

        mark_logs(("active chain height = %d: testing before sidechain fork" %  self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        self.doTest()

        # reach the fork where certificates are supported
        self.nodes[0].generate(20) 
        self.sync_all()

        mark_logs(("active chain height = %d: testing after sidechain fork" %  self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)

        # create a sidechain and a certificate for it in the mempool
        scid = "22"

        self.nodes[1].sc_create(scid, SC_EPOCH_LENGTH, "dada", SC_CREATION_AMOUNT)
        self.sync_all()

        block_list = self.nodes[0].generate(SC_EPOCH_LENGTH) 
        self.sync_all()

        pkh = self.nodes[0].getnewaddress("", True)
        amounts = [{"pubkeyhash": pkh, "amount": SC_CERT_AMOUNT}]
        fee = 0.000023
        cert = self.nodes[0].send_certificate(scid, 0, block_list[-1], amounts, fee)
        self.sync_all()
        assert_true(cert in self.nodes[0].getrawmempool() ) 

        # just one more tx, for having 3 generic txobjs and testing malleability of cert (Test 4)
        tx = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 0.01)
        self.sync_all()
        assert_true(tx in self.nodes[0].getrawmempool() ) 

        self.doTest()

        self.nodes[0].generate(1) 
        self.sync_all()


    def doTest(self):

        node = self.nodes[0]

        tmpl = node.getblocktemplate()
        if 'coinbasetxn' not in tmpl:
            rawcoinbase = encodeUNum(tmpl['height'])
            rawcoinbase += b'\x01-'
            hexcoinbase = b2x(rawcoinbase)
            hexoutval = b2x(pack('<Q', tmpl['coinbasevalue']))
            tmpl['coinbasetxn'] = {'data': '01000000' + '01' + '0000000000000000000000000000000000000000000000000000000000000000ffffffff' + ('%02x' % (len(rawcoinbase),)) + hexcoinbase + 'fffffffe' + '01' + hexoutval + '00' + '00000000'}
        txlist = list(bytearray(a2b_hex(a['data'])) for a in (tmpl['coinbasetxn'],) + tuple(tmpl['transactions']))
        certlist = []

        # if the block supports certificates, add them (if any)
        if tmpl['version'] == SC_CERTIFICATE_BLOCK_VERSION:
            certlist = list(bytearray(a2b_hex(a['data'])) for a in tuple(tmpl['certificates']))

        # Test 0: Capability advertised
        assert('proposal' in tmpl['capabilities'])

        # NOTE: This test currently FAILS (regtest mode doesn't enforce block height in coinbase)
        ## Test 1: Bad height in coinbase
        #txlist[0][4+1+36+1+1] += 1
        #assert_template(node, tmpl, txlist, 'FIXME')
        #txlist[0][4+1+36+1+1] -= 1

        # Test 2: Bad input hash for gen tx
        txlist[0][4+1] += 1
        assert_template(node, tmpl, txlist, certlist, 'bad-cb-missing')
        txlist[0][4+1] -= 1

        # Test 3: Truncated final tx
        lastbyte = txlist[-1].pop()
        try:
            assert_template(node, tmpl, txlist, certlist, 'n/a')
        except JSONRPCException:
            pass  # Expected
        txlist[-1].append(lastbyte)

        # Check for merkle tree malleability (CVE-2012-2459): repeating sequences
        # of transactions (or certificates) in a block without affecting the merkle root,
        # while still invalidating it.
        if len(certlist) == 0:
            # Test 4: Add an invalid tx to the end (duplicate of gen tx)
            txlist.append(txlist[0])
            assert_template(node, tmpl, txlist, certlist, 'bad-txns-duplicate')
            txlist.pop()
        else:
            # Test 4: Add an invalid cert to the end (duplicate of cert)
            certlist.append(certlist[0])
            assert_template(node, tmpl, txlist, certlist, 'bad-txns-duplicate')
            certlist.pop()

        # Test 5: Add an invalid tx to the end (non-duplicate)
        txlist.append(bytearray(txlist[0]))
        txlist[-1][4+1] = b'\xff'
        #! This transaction is failing sooner than intended in the
        #! test because of the lack of an op-checkblockheight
        #assert_template(node, tmpl, txlist, 'bad-txns-inputs-missingorspent') 
        assert_template(node, tmpl, txlist, certlist, 'op-checkblockatheight-needed')
        txlist.pop()

        # Test 6: Future tx lock time
        txlist[0][49] -= 1                      # in template nSequence is equal 0xffffffff, in such case it disables nLockTime. Decrease nSequence to enable lock time check.
        txlist[0][-4:] = b'\xff\xff\xff\xff'    # set nLockTime far in future
        assert_template(node, tmpl, txlist, certlist, 'bad-txns-nonfinal')
        txlist[0][-4:] = b'\0\0\0\0'

        # Test 7: Bad tx count
        txlist.append(b'')
        try:
            assert_template(node, tmpl, txlist, certlist, 'n/a')
        except JSONRPCException:
            pass  # Expected
        txlist.pop()

        # Test 8: Bad bits
        realbits = tmpl['bits']
        tmpl['bits'] = '1c0000ff'  # impossible in the real world
        assert_template(node, tmpl, txlist, certlist, 'bad-diffbits')
        tmpl['bits'] = realbits

        # Test 9: Bad merkle root
        rawtmpl = template_to_bytes(tmpl, txlist, certlist)
        rawtmpl[4+32] = (rawtmpl[4+32] + 1) % 0x100
        rsp = node.getblocktemplate({'data':b2x(rawtmpl),'mode':'proposal'})
        if rsp != 'bad-txnmrklroot':
            raise AssertionError('unexpected: %s' % (rsp,))

        # Test 10: Bad timestamps
        realtime = tmpl['curtime']
        tmpl['curtime'] = 0x7fffffff
        assert_template(node, tmpl, txlist, certlist, 'time-too-new')
        tmpl['curtime'] = 0
        assert_template(node, tmpl, txlist, certlist, 'time-too-old')
        tmpl['curtime'] = realtime

        if len(certlist) == 0:
            # Test 11: Valid block
            assert_template(node, tmpl, txlist, certlist, None)
        else:
            # compute commitment for the only contribution of certificate (we have no sctx/btr)
            TxsHash = dblsha(SC_NULL_HASH + SC_NULL_HASH)
            WCertHash = dblsha(certlist[0])
            scid = certlist[0][4:4+32]
            SCHash = dblsha(TxsHash + WCertHash + scid)
            assert_template(node, tmpl, txlist, certlist, None, SCHash)

        # Test 12: Orphan block
        orig_val = tmpl['previousblockhash']
        tmpl['previousblockhash'] = 'ff00' * 16
        assert_template(node, tmpl, txlist, certlist, 'inconclusive-not-best-prevblk')
        tmpl['previousblockhash'] = orig_val

        if len(certlist) != 0:
            # cert only specific tests

            # Test 13: Bad scid in cert
            # set scid to 0x21 (33)
            orig_val = certlist[0][4]
            certlist[0][4] = 33
            assert_template(node, tmpl, txlist, certlist, 'bad-sc-cert-not-applicable')
            certlist[0][4] = orig_val

            # Test 14: Bad cert count
            certlist.append(b'')
            try:
                assert_template(node, tmpl, txlist, certlist, 'n/a')
            except JSONRPCException:
                pass  # Expected
                certlist.pop()

            # Test 15: Truncated final cert
            lastbyte = certlist[-1].pop()
            try:
                assert_template(node, tmpl, txlist, certlist, 'n/a')
            except JSONRPCException:
                pass  # Expected
            certlist[-1].append(lastbyte)

            # Test 16: change epoch number
            orig_val = certlist[0][4 + 32]
            certlist[0][4 + 32] = 33 
            assert_template(node, tmpl, txlist, certlist, 'bad-sc-cert-not-applicable')
            certlist[0][4 + 32] = orig_val 

            # Test 17: wrong commitment
            # compute commitment for the only contribution of certificate (no tx/btr)
            fake_commitment = dblsha(b'\w'*32)
            assert_template(node, tmpl, txlist, certlist, 'bad-sc-txs-committment', fake_commitment)


if __name__ == '__main__':
    GetBlockTemplateProposalTest().main()
