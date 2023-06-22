#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import ForkHeights
from test_framework.authproxy import JSONRPCException
from test_framework.util import COIN, assert_true, assert_false, assert_equal, mark_logs, swap_bytes
from test_framework.mininode import hash256, ser_string
from test_framework.mc_test.mc_test import *

from binascii import a2b_hex, b2a_hex
from hashlib import sha256
from decimal import Decimal
from struct import unpack, pack

import binascii
import codecs

# this is defined in src/primitives/block.h
SC_CERTIFICATE_BLOCK_VERSION = 3

# this is defined in src/primitives/block.cpp
SC_NULL_HASH = hash256(ser_string(b"Horizen ScTxsCommitment null hash string"))

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

def template_to_bytearray(tmpl, txlist, certlist, input_sc_commitment = None):
    blkver = pack('<L', tmpl['version'])
    objlist = txlist + certlist
    mrklroot = genmrklroot(list(dblsha(a) for a in objlist))
    sc_commitment = b'\0'*32
    if input_sc_commitment != None:
        sc_commitment = input_sc_commitment
        mark_logs(("sc_commitment set in block template: %s" % swap_bytes(binascii.hexlify(sc_commitment))), NODE_LIST, DEBUG_MODE)
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
    return bytearray(blk)

def template_to_hex(tmpl, txlist, certlist, input_sc_commitment):
    return b2x(template_to_bytearray(tmpl, txlist, certlist, input_sc_commitment))

def assert_template(node, tmpl, txlist, certlist, expect, input_sc_commitment = None):
#    try:
#        print "list=",template_to_hex(tmpl, txlist)
#        raw_input("Pres to continue 1...")
     rsp = node.getblocktemplate({'data':template_to_hex(tmpl, txlist, certlist, input_sc_commitment),'mode':'proposal'})
     if rsp != expect:
         print("expect: ", expect, ", rsp: ", rsp)
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

        sc_fork_reached = False
        currentHeight = self.nodes[0].getblockcount()
        mark_logs(("active chain height = %d: testing before sidechain fork" % currentHeight), self.nodes, DEBUG_MODE)
        self.doTest(sc_fork_reached)

        # reach the height where the next block is the last before the fork point where certificates are supported
        delta = ForkHeights['MINIMAL_SC'] - currentHeight - 2
        self.nodes[0].generate(delta) 
        self.sync_all()

        mark_logs(("active chain height = %d: testing last block before sidechain fork" %  self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        self.doTestJustBeforeScFork()

        # reach the fork where certificates are supported
        self.nodes[0].generate(1) 
        self.sync_all()

        mark_logs(("active chain height = %d: testing block which will be at sidechain fork" %  self.nodes[0].getblockcount()), self.nodes, DEBUG_MODE)
        sc_fork_reached = True

        # create a sidechain and a certificate for it in the mempool
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()

        cmdInput = {
            'version': 0,
            'withdrawalEpochLength': SC_EPOCH_LENGTH,
            'toaddress': "dada",
            'amount': SC_CREATION_AMOUNT,
            'wCertVk': vk,
            'customData': "bb" * 1024,
            'constant': constant
        }
        ret = self.nodes[1].sc_create(cmdInput)
        creating_tx = ret['txid']
        scid = ret['scid']
        self.sync_all()

        decoded_tx = self.nodes[1].getrawtransaction(creating_tx, 1)
        assert_equal(scid, decoded_tx['vsc_ccout'][0]['scid'])
        mark_logs("created SC id: {}".format(scid), self.nodes, DEBUG_MODE)

        current_height = self.nodes[1].getblockcount()
        block_list = self.nodes[0].generate(SC_EPOCH_LENGTH) 
        self.sync_all()

        addr_node0 = self.nodes[0].getnewaddress()
        amounts = [{"address": addr_node0, "amount": SC_CERT_AMOUNT}]

        #create wCert proof
        epoch_cum_tree_hash = self.nodes[0].getblock(block_list[-1])['scCumTreeHash']
        ftScFee = 0.1
        mbtrScFee = 0.1
        fee = 0.000023

        scid_swapped = str(swap_bytes(scid))
        proof = mcTest.create_test_proof("sc1",
                                         scid_swapped,
                                         0,
                                         0,
                                         mbtrScFee,
                                         ftScFee,
                                         epoch_cum_tree_hash,
                                         constant = constant,
                                         pks      = [addr_node0],
                                         amounts  = [SC_CERT_AMOUNT])
        cert = self.nodes[0].sc_send_certificate(scid, 0, 0, epoch_cum_tree_hash, proof, amounts, ftScFee, mbtrScFee, fee)
        self.sync_all()
        assert_true(cert in self.nodes[0].getrawmempool() ) 
        mark_logs("cert issued : {}".format(cert), self.nodes, DEBUG_MODE)

        # just one more tx, for having 3 generic txobjs and testing malleability of cert (Test 4)
        tx = self.nodes[0].sendtoaddress(self.nodes[0].getnewaddress(), 0.01)
        self.sync_all()
        assert_true(tx in self.nodes[0].getrawmempool() ) 

        mark_logs("starting test: fork{}".format(sc_fork_reached), self.nodes, DEBUG_MODE)
        self.doTest(sc_fork_reached)

        self.nodes[0].generate(1) 
        self.sync_all()


    def doTestJustBeforeScFork(self):

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

        # Test: set a non-zero 'hashReserved' field (32 bytes after mkl tree field); this is used from the sc fork on, renamed as 'scTxsCommitment'
        rawtmpl = template_to_bytearray(tmpl, txlist, certlist)

        # a 32 null byte array string
        nb1 = b2x(bytearray(32))

        nb2 = b2x(rawtmpl[4+32+32:4+32+32+32])
        # check hashReserved field is currently null 
        assert_true(nb1 == nb2)

        for j in range(0,32):
            rawtmpl[4+32+32+j] = j

        # hashReserved is not null now
        nb3 = b2x(rawtmpl[4+32+32:4+32+32+32])
        assert_false(nb3 == nb2)

        rsp = node.getblocktemplate({'data':b2x(rawtmpl),'mode':'proposal'})
        # assert block validity
        assert_equal(rsp, None)



    def doTest(self, sc_fork_reached):

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
            assert_true(sc_fork_reached)
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
        txlist[-1][4+1] = 0xff
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
        rawtmpl = template_to_bytearray(tmpl, txlist, certlist)
        rawtmpl[4+32] = (rawtmpl[4+32] + 1) % 0x100
        rsp = node.getblocktemplate({'data':b2x(rawtmpl),'mode':'proposal'})
        if rsp != 'bad-txnmrklroot':
            raise AssertionError('unexpected: %s' % (rsp,))

        # Test 10: Bad timestamps
        realtime = tmpl['curtime']
        tmpl['curtime'] = 0x7fffffff
        if sc_fork_reached == False:
            assert_template(node, tmpl, txlist, certlist, 'time-too-new')
        else:
            # if we reached sc fork we also have passed timeblock fork and the error changes
            assert_template(node, tmpl, txlist, certlist, 'time-too-far-ahead-of-mtp')
        tmpl['curtime'] = 0
        assert_template(node, tmpl, txlist, certlist, 'time-too-old')
        tmpl['curtime'] = realtime

        if sc_fork_reached == False:
            # Test 11: Valid block
            assert_template(node, tmpl, txlist, certlist, None)
        else:
            assert_true(len(certlist) != 0)
            # compute commitment for the only contribution of certificate (we have no sctx/btr)
            '''
            TODO - this test is commented out since in this branch the sc commitment tree is computed in a different way
            in mainchain and it would trigger an error 
            -----
            TxsHash = dblsha(SC_NULL_HASH + SC_NULL_HASH)
            WCertHash = dblsha(certlist[0])
            scid = certlist[0][4:4+32]
            SCHash = dblsha(TxsHash + WCertHash + scid)
            assert_template(node, tmpl, txlist, certlist, None, SCHash)
            '''

        # Test 12: Orphan block
        orig_val = tmpl['previousblockhash']
        tmpl['previousblockhash'] = 'ff00' * 16
        assert_template(node, tmpl, txlist, certlist, 'inconclusive-not-best-prevblk')
        tmpl['previousblockhash'] = orig_val

        if sc_fork_reached == True:
            assert_true(len(certlist) != 0)
            # cert only specific tests

            # Test 13: Bad cert count
            mark_logs("Bad cert count (expecting failure...)", NODE_LIST, DEBUG_MODE)
            certlist.append(b'')
            try:
                assert_template(node, tmpl, txlist, certlist, 'n/a')
            except JSONRPCException:
                pass  # Expected
                certlist.pop()

            # Test 14: Truncated final cert
            mark_logs("Truncated final cert (expecting failure...)", NODE_LIST, DEBUG_MODE)
            lastbyte = certlist[-1].pop()
            try:
                assert_template(node, tmpl, txlist, certlist, 'n/a')
            except JSONRPCException:
                pass  # Expected
            certlist[-1].append(lastbyte)

            # Test 15: invalid field element as a commitment tree
            fake_commitment = (b'\xff'*32)
            fake_commitment_str = binascii.hexlify(fake_commitment)
            assert_template(node, tmpl, txlist, certlist, 'invalid-sc-txs-commitment', fake_commitment)

            # Test 16: wrong commitment, the block will be rejected because it is different from the one computed using tx/certs
            rnd_fe = generate_random_field_element_hex()
            fake_commitment = a2b_hex(rnd_fe)
            assert_template(node, tmpl, txlist, certlist, 'bad-sc-txs-commitment', fake_commitment)


if __name__ == '__main__':
    GetBlockTemplateProposalTest().main()
