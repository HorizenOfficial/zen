#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Exercise the getblockmerkleroots RPC function

from test_framework.test_framework import BitcoinTestFramework
from decimal import Decimal
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, wait_and_assert_operationid_status
from test_framework.mc_test.mc_test import CertTestUtils, generate_random_field_element_hex



class GetBlockMerkleRootsTest(BitcoinTestFramework):

    def verify_roots(self, block):
        txs = []
        for tx in block["tx"]:
            txs += [self.nodes[0].getrawtransaction(tx)]

        rpcResponse = self.nodes[0].getblockmerkleroots(txs,[])
        assert_equal(rpcResponse["merkleTree"], block["merkleroot"])
        assert_equal(rpcResponse["scTxsCommitment"], block["scTxsCommitment"])

    def run_test(self):
        self.nodes[0].generate(110)
        self.sync_all()

        #Test getblockmerkleroots with invalid argument
        print("######## Test getblockmerkleroots with invalid argument ########")
        errorString = ""
        try:
            self.nodes[0].getblockmerkleroots([])
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            assert_equal("getblockmerkleroots transactions certificates" in errorString, True)

        try:
            self.nodes[0].getblockmerkleroots([""],[""])
            assert(True)
        except JSONRPCException, e:
            errorString = e.error['message']
            assert_equal("TX decode failed" in errorString, True)

        #Test getblockmerkleroots with only coinbase transaction before the sidechain fork
        print("######## Test getblockmerkleroots with only coinbase transaction ########")

        block = self.nodes[0].getblock('110')
        self.verify_roots(block)

        #Reach the sidechain fork height
        self.nodes[0].generate(311)
        self.sync_all()

        #Test getblockmerkleroots with some t-transactions
        print("######## Test getblockmerkleroots with some transactions ########")

        tAddr = self.nodes[0].getnewaddress()

        for _ in range (0,3):
            self.nodes[0].sendtoaddress(tAddr, 1.0)
            self.sync_all()
        self.nodes[0].generate(1)

        block = self.nodes[0].getblock('111')
        self.verify_roots(block)

        #Test getblockmerkleroots with some t/z-transactions
        print("######## Test getblockmerkleroots with some transactions ########")

        zAddr = self.nodes[0].z_getnewaddress()
        recipients= [{"address":zAddr, "amount": 2.0}]
        myopid = self.nodes[0].z_sendmany(tAddr,recipients,1,0.0001, True)
        wait_and_assert_operationid_status(self.nodes[0], myopid)
        self.sync_all()

        for _ in range (0,3):
            self.nodes[0].sendtoaddress(tAddr, 1.0)
            self.sync_all()
        
        self.nodes[0].generate(1)
        self.sync_all()

        block = self.nodes[0].getblock('112')
        self.verify_roots(block)  

        #Test getblockmerkleroots with a sc-creation
        print("######## Test getblockmerkleroots with a sc-creation ########")

        self.nodes[0].generate(108)
        self.sync_all()

        #generate wCertVk and constant
        mcTest = CertTestUtils(self.options.tmpdir, self.options.srcdir)
        vk = mcTest.generate_params("sc1")
        constant = generate_random_field_element_hex()

        ret = self.nodes[0].sc_create(123, "dada", 3.0, vk, "bb" * 1024, constant)
        scid = ret['scid']
        self.sync_all()
        self.nodes[0].sendtoaddress(tAddr, 1.0)
        self.nodes[0].generate(1)
        self.sync_all()

        block = self.nodes[0].getblock('221')
        self.verify_roots(block)  

        #Test getblockmerkleroots with a FT
        print("######## Test getblockmerkleroots with a FT ########")

        self.nodes[0].sc_send("abcd", 2.0, scid)
        self.sync_all()
        self.nodes[0].sendtoaddress(tAddr, 1.0)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all() 
 
        block = self.nodes[0].getblock('222')
        self.verify_roots(block)  



if __name__ == '__main__':
    GetBlockMerkleRootsTest().main()
