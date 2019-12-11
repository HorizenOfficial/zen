#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Exercise the listtransactions API

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, connect_nodes_bi

from decimal import Decimal

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

class ListTransactionsTest(BitcoinTestFramework):
    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_network(self, split=False):
        self.nodes = start_nodes(3, self.options.tmpdir)

        #connect to a local machine for debugging
        #url = "http://bitcoinrpc:DP6DvqZtqXarpeNWyN3LZTFchCCyCUuHwNF7E8pX99x1@%s:%d" % ('127.0.0.1', 18232)
        #proxy = AuthServiceProxy(url)
        #proxy.url = url # store URL on proxy for info
        #self.nodes.append(proxy)

        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)

        self.is_network_split=False
        self.sync_all()

    def run_test(self):

	self.nodes[0].generate(120)
	address=self.nodes[1].getnewaddress()
	


	#simple send 1 to address and verify listtransaction returns this tx with address in input
	txid=self.nodes[0].sendtoaddress(address, float(1) )
	self.sync_all()
	self.nodes[2].generate(1)
        self.sync_all()
	check_array_result(self.nodes[1].listtransactions(address),
                           {"txid":txid},
                           {"amount":Decimal("1.0")})

	#verify listtransactions returns this tx without any input
	check_array_result(self.nodes[1].listtransactions(address),
                           {"txid":txid},
                           {"amount":Decimal("1.0")})

	#verify listtransactions returns only the tx with a specific address
	txid2=self.nodes[0].sendtoaddress(self.nodes[1].getnewaddress(), float(1) )
	self.sync_all()
	self.nodes[2].generate(1)
        self.sync_all()
	result=self.nodes[1].listtransactions(address)
	if(len(result)!=1):
		raise AssertionError("Expected only 1 transaction")
	check_array_result(result,
                           {"txid":txid},
                           {"amount":Decimal("1.0"),"address":address})
	
	#verify listtransactions returns 2 tx with no inputs
	result=self.nodes[1].listtransactions()
	if(len(result)!=2):
		raise AssertionError("Expected 2 transactions")

	#verify listtransactions returns only last 10 tx with address in input
	txes=[]
	for i in range(1,11):
		txid=self.nodes[0].sendtoaddress(address, float(i) )
		txes.append(txid)		
		self.sync_all()
		self.nodes[2].generate(1)
        	self.sync_all()

	result=self.nodes[1].listtransactions(address)
	if(len(result)!=10):
		raise AssertionError("Expected only 10 transactions")
	for i in range(11,1):
		check_array_result([result[i-1]],
			   {"txid":txes[i-1]},
                           {"amount":float(i),"address":address})


	#verify listtransactions returns the 5 most recent transactions of address
	result=self.nodes[1].listtransactions(address,5)	
	if(len(result)!=5):
		raise AssertionError("Expected only 10 transactions")
	for i in range(11,7):
		check_array_result([result[i-1]],
			   {"txid":txes[i-1]},
                           {"amount":float(i),"address":address})


	#verify listtransactions returns the transactions n.3-4-5-6-7 of address
	result=self.nodes[1].listtransactions(address,5,3)
	if(len(result)!=5):
		raise AssertionError("Expected only 10 transactions")
	for i in range(8,4):
		print("I: ",result[i-1])
		check_array_result([result[i-1]],
			   {"txid":txes[i-1]},
                           {"amount":float(i),"address":address})



if __name__ == '__main__':
    ListTransactionsTest().main()

