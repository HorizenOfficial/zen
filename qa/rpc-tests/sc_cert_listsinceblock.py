#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
from test_framework.test_framework import BitcoinTestFramework
from test_framework.test_framework import MINIMAL_SC_HEIGHT
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_false, assert_true, assert_equal, initialize_chain_clean, \
    start_nodes, connect_nodes_bi, mark_logs, \
    get_epoch_data, swap_bytes, get_spendable 
from test_framework.mc_test.mc_test import CertTestUtils, generate_random_field_element_hex
import os
import pprint
from decimal import Decimal
from test_framework.mininode import COIN

NUMB_OF_NODES = 3
DEBUG_MODE = 1
EPOCH_LENGTH = 20
FT_SC_FEE = Decimal('0')
MBTR_SC_FEE = Decimal('0')
CERT_FEE = Decimal("0.00025")
CUSTOM_FEE_RATE_ZAT_PER_BYTE = Decimal('20.0')
CUSTOM_FEE_RATE_ZEN_PER_KBYTE = CUSTOM_FEE_RATE_ZAT_PER_BYTE/COIN*1000

class ScCertListsinceblock(BitcoinTestFramework):
    alert_filename = None

    def setup_chain(self):
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)
        self.alert_filename = os.path.join(self.options.tmpdir, "alert.txt")
        with open(self.alert_filename, 'w'):
            pass  # Just open then close to create zero-length file

    def setup_network(self, split=False):

        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir,
            extra_args=[['-logtimemicros=1', '-debug=cert', '-debug=sc', '-debug=py']]*NUMB_OF_NODES)

        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)

        self.is_network_split = split
        self.sync_all()

    def run_test(self):
        '''
        Test that the JSON result of the rpc command listsinceblock includes a matured certificate backward transfer even when:
        1) the input block range specified does not contain the block where such certificate has been mined.
        2) The certificate matures in one of the blocks of the specified range
        '''

        mark_logs("Node 1 generates 2 block",self.nodes,DEBUG_MODE)
        self.nodes[1].generate(2)
        self.sync_all()

        mark_logs("Node 0 generates {} block".format(MINIMAL_SC_HEIGHT-2),self.nodes,DEBUG_MODE)
        self.nodes[0].generate(MINIMAL_SC_HEIGHT-2)
        self.sync_all()

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params('sc1')
        constant = generate_random_field_element_hex()

        # create SC
        #------------------------------------------------------------------------------------------------------------
        cmdInput = {
            'version':0,
            'toaddress': "abcd",
            'amount': 20.0,
            'wCertVk': vk,
            'withdrawalEpochLength': EPOCH_LENGTH,
            'constant': constant
        }

        mark_logs("\nNode 1 create SC", self.nodes, DEBUG_MODE)
        try:
            res = self.nodes[1].sc_create(cmdInput)
            tx =   res['txid']
            scid = res['scid']
            pprint.pprint(res)
            self.sync_all()
        except JSONRPCException as e:
            errorString = e.error['message']
            mark_logs(errorString,self.nodes,DEBUG_MODE)
            assert_true(False)

        mark_logs("\nNode 0 generates 1 block", self.nodes, DEBUG_MODE)
        bl = self.nodes[0].generate(1)[-1]
        self.sync_all()

        item = self.nodes[0].getscinfo(scid)['items'][0]
        elen = item['withdrawalEpochLength']
        wlen = item['certSubmissionWindowLength']
        ch   = item['createdAtBlockHeight']

        scid_swapped = str(swap_bytes(scid))

        q = 10
        blocks_d = {}
        block_heights_d = {}
        certs_d = {}
        mat_height_d = {}

        taddr1 = self.nodes[1].getnewaddress()
        taddr2 = self.nodes[2].getnewaddress()

        # advance some epochs and send a certificate for any of them
        for i in range(3):

            am_bwt1 = Decimal(i+1) + Decimal('0.01')
            am_bwt2 = Decimal(i+1) + Decimal('0.02')
            am_out  = Decimal('0.001') * (i+1)

            mark_logs("Advance epoch...", self.nodes, DEBUG_MODE)
            self.nodes[0].generate(EPOCH_LENGTH - 1)
            self.sync_all()
            epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, self.nodes[0], EPOCH_LENGTH)
 
            mark_logs("Node 1 sends a cert", self.nodes, DEBUG_MODE)
            #==============================================================
            pkh_arr = []
            am_bwt_arr = []
            raw_bwt_outs = [
                {"address": taddr1, "amount": am_bwt1},
                {"address": taddr2, "amount": am_bwt2}
            ]
            for entry in raw_bwt_outs:
                pkh_arr.append(entry["address"])
                am_bwt_arr.append(entry["amount"])
 
            proof = mcTest.create_test_proof(
                "sc1", scid_swapped, epoch_number, q, MBTR_SC_FEE, FT_SC_FEE, epoch_cum_tree_hash, constant,
                pkh_arr, am_bwt_arr)
            
            utx, change = get_spendable(self.nodes[1], CERT_FEE + am_out)
            raw_inputs  = [ {'txid' : utx['txid'], 'vout' : utx['vout']}]
            raw_outs    = { taddr1: change, taddr2: am_out}
 
            raw_params = {
                "scid": scid, "quality": q, "endEpochCumScTxCommTreeRoot": epoch_cum_tree_hash,
                "scProof": proof, "withdrawalEpochNumber": epoch_number
            }
 
            # use the raw version of the command for having more than one standard output
            # -------------------------------------
            # vout 1: standard output (taddr1) - change
            # vout 2: standard output (taddr2)
            # vout 3: bwt (taddr1)
            # vout 3: bwt (taddr2)
            try:
                raw_cert    = self.nodes[1].createrawcertificate(raw_inputs, raw_outs, raw_bwt_outs, raw_params)
                signed_cert = self.nodes[1].signrawtransaction(raw_cert)
                certs_d[i] = self.nodes[1].sendrawtransaction(signed_cert['hex'])
            except JSONRPCException as e:
                errorString = e.error['message']
                print("\n======> ", errorString)
                assert_true(False)

            self.sync_all()

            mark_logs("cert = {}".format(certs_d[i]), self.nodes, DEBUG_MODE)
            mat_height_d[i] = ch - 1 + (i+2)*elen + wlen
            print ("mat height = {}".format(mat_height_d[i]))
  
            mark_logs("Node 0 generates 1 block", self.nodes, DEBUG_MODE)
            bl = self.nodes[0].generate(1)[-1]
            self.sync_all()
            c = self.nodes[0].getblockcount()
            blocks_d[i] = bl
            block_heights_d[i] = c
 
        # the first of the 3 certificates has reached maturity and Node2 has a consistent balance
        bal = self.nodes[2].getbalance()
        print ("Node2 balance = {}".format(bal))
        assert_equal(bal, Decimal('1.02') + Decimal('0.001') + Decimal('0.002') + Decimal('0.003'))

        mark_logs("Calling listsinceblock on Node2 for all transactions", self.nodes, DEBUG_MODE)
        ret = self.nodes[2].listsinceblock("", 1, False, True)
        # pprint.pprint(ret)

        mat_block_hash    = self.nodes[2].getblockhash(mat_height_d[0])
        # first cert is there and its backward transfer to Node2 is mature
        # we also have one ordinary output from this cert, and we should have no maturity info 
        cert = certs_d[0]
        found_bwt = False
        found_out = False
        for x in ret['transactions']:
            if x['txid'] == cert:
                assert_equal(x['category'], 'receive')
                assert_equal(x['blockhash'], blocks_d[0])
                if 'isBackwardTransfer' in x:
                    assert_equal(x['amount'],   Decimal('1.02'))
                    assert_equal(x['isBackwardTransfer'], True)
                    assert_equal(x['maturityblockhash'], mat_block_hash)
                    found_bwt = True
                else:
                    assert_false('maturityblockhash' in x)
                    assert_equal(x['amount'],   Decimal('0.001'))
                    found_out = True
        assert_true(found_bwt)
        assert_true(found_out)

        # cert 2 is there and is immature
        # we also have one ordinary output from this cert 
        cert = certs_d[1]
        found_bwt = False
        found_out = False
        for x in ret['transactions']:
            if x['txid'] == cert:
                assert_equal(x['blockhash'], blocks_d[1])
                if 'isBackwardTransfer' in x:
                    assert_equal(x['category'], 'immature')
                    assert_equal(x['amount'],   Decimal('2.02'))
                    assert_equal(x['isBackwardTransfer'], True)
                    found_bwt = True
                else:
                    assert_equal(x['category'], 'receive')
                    assert_equal(x['amount'],   Decimal('0.002'))
                    found_out = True
        assert_true(found_bwt)
        assert_true(found_out)

        # last cert is there and is immature
        # we also have one ordinary output from this cert 
        cert = certs_d[2]
        found_bwt = False
        found_out = False
        for x in ret['transactions']:
            if x['txid'] == cert:
                assert_equal(x['blockhash'], blocks_d[2])
                if 'isBackwardTransfer' in x:
                    assert_equal(x['category'], 'immature')
                    assert_equal(x['amount'],   Decimal('3.02'))
                    assert_equal(x['isBackwardTransfer'], True)
                    found_bwt = True
                else:
                    assert_equal(x['category'], 'receive')
                    assert_equal(x['amount'],   Decimal('0.003'))
                    found_out = True
        assert_true(found_bwt)
        assert_true(found_out)

        # calling the cmd targeting the block where the second certificate has been mined
        mark_logs("Calling listsinceblock on Node2 for h={}, hash={}".format(block_heights_d[1], blocks_d[1]), self.nodes, DEBUG_MODE)
        ret = self.nodes[2].listsinceblock(blocks_d[1], 1, False, True)
        pprint.pprint(ret)
        
        cert = certs_d[0]
        # first cert is there, it is mature and it refers to the block where it matured
        # we should not see the standard output anymore 
        mat_block_hash    = self.nodes[2].getblockhash(mat_height_d[0])
        found = False
        for x in ret['transactions']:
            if x['txid'] == cert:
                found = True
                assert_equal(x['amount'],   Decimal('1.02'))
                assert_equal(x['category'], 'receive')
                assert_equal(x['blockhash'], blocks_d[0])
                assert_equal(x['maturityblockhash'], mat_block_hash)
                assert_equal(x['isBackwardTransfer'], True)
        assert_true(found)

        # cert 2 is not there (since-block is not part of the range) 
        cert = certs_d[1]
        found = False
        for x in ret['transactions']:
            if x['txid'] == cert:
                found = True
        assert_false(found)

        # last cert is there and is immature, we have also standard output
        cert = certs_d[2]
        found_bwt = False
        found_out = False
        for x in ret['transactions']:
            if x['txid'] == cert:
                assert_equal(x['blockhash'], blocks_d[2])
                if 'isBackwardTransfer' in x:
                    assert_equal(x['category'], 'immature')
                    assert_equal(x['amount'],   Decimal('3.02'))
                    assert_equal(x['isBackwardTransfer'], True)
                    found_bwt = True
                else:
                    assert_equal(x['category'], 'receive')
                    assert_equal(x['amount'],   Decimal('0.003'))
                    found_out = True
        assert_true(found_bwt)
        assert_true(found_out)

        # calling the cmd targeting the block where the last certificate has been mined
        # There must be no certificates at all, since none of them is contained or matures in this block range 
        mark_logs("Calling listsinceblock on Node2 for h={}, hash={}".format(block_heights_d[2], blocks_d[2]), self.nodes, DEBUG_MODE)
        ret = self.nodes[2].listsinceblock(blocks_d[2], 1, False, True)
        assert_true(len(ret['transactions']) == 0)

        # reach the height of the second certificate and re-issue the command
        c = self.nodes[0].getblockcount()
        mark_logs("Node 0 generates {} block".format(mat_height_d[1] - c),self.nodes,DEBUG_MODE)
        self.nodes[0].generate(mat_height_d[1] - c)
        self.sync_all()
        print ("chain height = {}".format(self.nodes[0].getblockcount()))

        mark_logs("Calling listsinceblock on Node2 for h={}, hash={}".format(block_heights_d[2], blocks_d[2]), self.nodes, DEBUG_MODE)
        ret = self.nodes[2].listsinceblock(blocks_d[2], 1, False, True)

        cert = certs_d[0]
        # first cert is not there
        found = False
        for x in ret['transactions']:
            if x['txid'] == cert:
                found = True
        assert_false(found)

        cert = certs_d[1]
        # second cert is there, it is mature and it refers to the block where it matured
        found = False
        mat_block_hash = self.nodes[2].getblockhash(mat_height_d[1])
        for x in ret['transactions']:
            if x['txid'] == cert:
                found = True
                assert_equal(x['amount'],   Decimal('2.02'))
                assert_equal(x['category'], 'receive')
                assert_equal(x['blockhash'], blocks_d[1])
                assert_equal(x['maturityblockhash'], mat_block_hash)
                assert_equal(x['isBackwardTransfer'], True)
        assert_true(found)

        cert = certs_d[2]
        # last cert is not there
        found = False
        for x in ret['transactions']:
            if x['txid'] == cert:
                found = True
        assert_false(found)

        # get the block before the one containing the last certificate
        h = block_heights_d[2] - 1
        bl = self.nodes[0].getblockhash(h)

        mark_logs("Calling listsinceblock on Node2 for h={}, hash={}".format(h, bl), self.nodes, DEBUG_MODE)
        ret = self.nodes[2].listsinceblock(bl, 1, False, True)

        # last cert is there and is immature
        # we also have one ordinary output from this cert 
        cert = certs_d[2]
        found_bwt = False
        found_out = False
        for x in ret['transactions']:
            if x['txid'] == cert:
                assert_equal(x['blockhash'], blocks_d[2])
                if 'isBackwardTransfer' in x:
                    assert_equal(x['category'], 'immature')
                    assert_equal(x['amount'],   Decimal('3.02'))
                    assert_equal(x['isBackwardTransfer'], True)
                    found_bwt = True
                else:
                    assert_equal(x['category'], 'receive')
                    assert_equal(x['amount'],   Decimal('0.003'))
                    found_out = True
        assert_true(found_bwt)
        assert_true(found_out)

        # reach the height of the third certificate, that will make the SC cease, and re-issue the command
        c = self.nodes[0].getblockcount()
        mark_logs("Node 0 generates {} block".format(mat_height_d[2] - c),self.nodes,DEBUG_MODE)
        self.nodes[0].generate(mat_height_d[2] - c)
        self.sync_all()
        print ("chain height = {}".format(self.nodes[0].getblockcount()))

        ret = self.nodes[0].getscinfo(scid, False, False)['items'][0]
        assert_equal(ret['ceasingHeight'], mat_height_d[2])
        assert_equal(ret['state'], "CEASED")
        
        mark_logs("Calling listsinceblock on Node2 for h={}, hash={}".format(h, bl), self.nodes, DEBUG_MODE)
        ret = self.nodes[2].listsinceblock(bl, 1, False, True)

        # last cert bwt is not there anymore as expected
        # but we still have one ordinary output from this cert 
        cert = certs_d[2]
        found = False
        for x in ret['transactions']:
            if x['txid'] == cert:
                found = True
                assert_false('isBackwardTransfer' in x)
                assert_equal(x['blockhash'], blocks_d[2])
                assert_equal(x['category'], 'receive')
                assert_equal(x['amount'],   Decimal('0.003'))
        assert_true(found)

if __name__ == '__main__':
    ScCertListsinceblock().main()
