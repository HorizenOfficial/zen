#!/usr/bin/env python3
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, initialize_chain_clean, \
    start_nodes, connect_nodes_bi, stop_node, wait_and_assert_operationid_status

import time

class ZSendmanyTest(BitcoinTestFramework):
    FEE = 0.0001

    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 3)

    def setup_network(self, split=False):
        self.nodes = start_nodes(3, self.options.tmpdir, extra_args=[['-debug=zrpcunsafe']] * 3 )
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,1,2)
        connect_nodes_bi(self.nodes,0,2)
        self.is_network_split=False
        self.sync_all()

    # Returns txid if operation was a success or None
    def wait_and_assert_operationid_status(self, myopid, in_status='success', in_errormsg=None):
        print('waiting for async operation {}'.format(myopid))
        opids = []
        opids.append(myopid)
        timeout = 500
        status = None
        errormsg = None
        txid = None
        for x in range(1, timeout):
            results = self.nodes[0].z_getoperationresult(opids)
            if len(results)==0:
                time.sleep(1)
            else:
                status = results[0]["status"]
                if status == "failed":
                    errormsg = results[0]['error']['message']
                elif status == "success":
                    txid = results[0]['result']['txid']
                break
        print('...returned status: {}'.format(status))
        assert_equal(in_status, status)
        if errormsg is not None:
            assert(in_errormsg is not None)
            assert_equal(in_errormsg in errormsg, True)
            print('...returned error: {}'.format(errormsg))
        return txid

    def run_test (self):
        print("Mining blocks...")

        self.nodes[0].generate(110)
        self.sync_all()

        assert_equal(self.nodes[0].getbalance(),11.4375*10)
        assert_equal(self.nodes[1].getbalance(),0)
        assert_equal(self.nodes[2].getbalance(),0)

        fromAddress = self.nodes[1].getnewaddress()
        self.nodes[0].sendtoaddress(fromAddress,50.0)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()
        assert_equal(self.nodes[1].getbalance(),50)

        amount = 10.0
        #Send 10 Zen to Zaddress and verify the change is returned back to the input address of the transaction
        ZDestAddress = self.nodes[2].z_getnewaddress()
        recipients= [{"address":ZDestAddress, "amount": amount}]
        myopid = self.nodes[1].z_sendmany(fromAddress,recipients,1,self.FEE, True)
        txid = wait_and_assert_operationid_status(self.nodes[1], myopid)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        hex = self.nodes[1].gettransaction(txid)['hex']
        vouts = self.nodes[1].decoderawtransaction(hex)['vout']
        assert_equal(len(vouts),1)
        assert_equal(vouts[0]['scriptPubKey']['addresses'][0],fromAddress)
        assert_equal(self.nodes[2].z_getbalance(ZDestAddress),10)


        #Send 10 Zen to Taddress and verify the change is returned back to the input address of the transaction
        TDestAddress = self.nodes[2].getnewaddress()
        recipients= [{"address":TDestAddress, "amount": amount}]
        myopid = self.nodes[1].z_sendmany(fromAddress,recipients,1,self.FEE, True)
        txid = wait_and_assert_operationid_status(self.nodes[1], myopid)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        hex = self.nodes[1].gettransaction(txid)['hex']
        vouts = self.nodes[1].decoderawtransaction(hex)['vout']
        assert_equal(len(vouts),2)
        oldAddress = True
        for vout in vouts:
            if (vout['scriptPubKey']['addresses'][0] != TDestAddress and vout['scriptPubKey']['addresses'][0] != fromAddress):
                oldAddress = False
        assert_equal(oldAddress,True)
        assert_equal(self.nodes[2].getbalance(),amount)


        #Send 10 Zen to Taddress and verify the change is sent to a new address
        recipients= [{"address":TDestAddress, "amount": amount}]
        myopid = self.nodes[1].z_sendmany(fromAddress,recipients,1,self.FEE, False)
        txid = wait_and_assert_operationid_status(self.nodes[1], myopid)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        hex = self.nodes[1].gettransaction(txid)['hex']
        vouts = self.nodes[1].decoderawtransaction(hex)['vout']
        assert_equal(len(vouts),2)
        newAddress = True
        for vout in vouts:
            if (vout['scriptPubKey']['addresses'][0] == fromAddress):
                newAddress = False
        assert_equal(newAddress,True)
        assert_equal(self.nodes[2].getbalance(),amount*2)


        #Send 10 Zen to Taddress and verify the change is sent to a new address
        recipients= [{"address":fromAddress, "amount": amount}]
        myopid = self.nodes[2].z_sendmany(TDestAddress,recipients)
        txid = wait_and_assert_operationid_status(self.nodes[2], myopid)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        hex = self.nodes[2].gettransaction(txid)['hex']
        vouts = self.nodes[2].decoderawtransaction(hex)['vout']
        assert_equal(len(vouts),2)
        newAddress = True
        for vout in vouts:
            if (vout['scriptPubKey']['addresses'][0] == TDestAddress):
                newAddress = False
        assert_equal(newAddress,True)

        #Send 10 Zen from a multiSig address and verify the change is returned back to the input address of the transaction
        Taddr1 = self.nodes[1].getnewaddress()
        Taddr2 = self.nodes[1].getnewaddress()
        addr1Obj = self.nodes[1].validateaddress(Taddr1)
        addr2Obj = self.nodes[1].validateaddress(Taddr2)
        mSigObj = self.nodes[1].addmultisigaddress(2, [addr1Obj['pubkey'], addr2Obj['pubkey']])
        mSigObjValid = self.nodes[1].validateaddress(mSigObj)

        self.nodes[0].sendtoaddress(mSigObj,15.0)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        recipients= [{"address":TDestAddress, "amount": 10.0}]
        myopid = self.nodes[1].z_sendmany(mSigObj,recipients,1,self.FEE, True)
        txid = wait_and_assert_operationid_status(self.nodes[1], myopid)
        self.sync_all()
        self.nodes[0].generate(1)
        self.sync_all()

        hex = self.nodes[1].gettransaction(txid)['hex']
        vouts = self.nodes[1].decoderawtransaction(hex)['vout']
        assert_equal(len(vouts),2)
        oldAddress = True
        for vout in vouts:
            if (vout['scriptPubKey']['addresses'][0] != mSigObj and vout['scriptPubKey']['addresses'][0] != TDestAddress):
                oldAddress = False
        assert_equal(oldAddress,True)

if __name__ == '__main__':
    ZSendmanyTest().main()