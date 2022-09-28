#!/usr/bin/env python3

from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, assert_greater_than, \
    initialize_chain_clean, start_nodes, start_node, connect_nodes_bi, \
    stop_nodes, sync_blocks, sync_mempools, wait_bitcoinds


class ReplayProtectionTest (BitcoinTestFramework):
    def setup_chain(self):
        print("Initializing test directory "+self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, 4)

    def setup_network(self, split=False):
        self.nodes = start_nodes(3, self.options.tmpdir)
        connect_nodes_bi(self.nodes, 0, 1)
        connect_nodes_bi(self.nodes, 1, 2)
        connect_nodes_bi(self.nodes, 0, 2)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        self.nodes[0].generate(1)
        self.sync_all()
        self.nodes[1].generate(101)
        self.sync_all()
        # create a new transaction
        txid = self.nodes[0].sendtoaddress(self.nodes[2].getnewaddress(), 6)
        self.sync_all()
        self.nodes[0].generate(3)
        self.nodes[1].generate(3)
        self.nodes[2].generate(3)
        self.sync_all()

        rtx = self.nodes[0].decoderawtransaction(
            self.nodes[0].getrawtransaction(txid))
        voutAsm = [x['scriptPubKey']['asm'] for x in rtx['vout']]

        confirmations = int(
            self.nodes[0].gettransaction(txid)['confirmations'])

        # Test whether the transaction has been successfully confirmed.
        assert (confirmations > 0)

        def checkParamBlockHash(param1, param2):
            for i in range(1, len(param1), 2):
                if(param1[i] != param2[i-1] or param1[i-1] != param2[i]):
                    return False
            return True

        for asm in voutAsm:
            # Test if the stack doesnt have fewer than 2 elements
            assert len(asm.split()) > 2
            op_i = asm.split().index('OP_CHECKBLOCKATHEIGHT')
            ParamHeight = asm.split()[op_i-1]
            # Test if ParamHeight is no larger than the current block height
            assert (int(ParamHeight) <= 111)
            blockhash_at_height = self.nodes[0].getblockhash(int(ParamHeight))
            ParamBlockHash = asm.split()[op_i-2]
            print(ParamBlockHash)
            print(blockhash_at_height)
            # Test if ParamBlockHash refers to the blockhash at height ParamHeight
            assert checkParamBlockHash(
                ParamBlockHash, blockhash_at_height[::-1])


if __name__ == '__main__':
    ReplayProtectionTest().main()
