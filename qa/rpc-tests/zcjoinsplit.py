#!/usr/bin/env python3

#
# Test joinsplit semantics
#

from test_framework.test_framework import BitcoinTestFramework, ForkHeights, sync_blocks
from test_framework.authproxy import JSONRPCException
from test_framework.util import assert_equal, start_node, \
    gather_inputs, connect_nodes, disconnect_nodes
from decimal import Decimal

import sys

RPC_VERIFY_REJECTED = -26

class JoinSplitTest(BitcoinTestFramework):
    def setup_network(self):
        self.nodes = []
        self.is_network_split = False
        self.nodes.append(start_node(0, self.options.tmpdir))
        self.nodes.append(start_node(1, self.options.tmpdir))
        connect_nodes(self.nodes[0], 1)

    def run_test(self):

        ForkHeight = ForkHeights['SHIELDED_POOL_DEPRECATION']

        # first round pre-fork, second round post-fork
        pre_fork_round = 0
        just_pre_fork_round = 1
        post_fork_round = 2
        for round in range(3):
            zckeypair = self.nodes[0].zcrawkeygen()
            zcsecretkey = zckeypair["zcsecretkey"]
            zcaddress = zckeypair["zcaddress"]

            (total_in, inputs) = gather_inputs(self.nodes[0], 45.75)

            protect_tx = self.nodes[0].createrawtransaction(inputs, {})
            joinsplit_result = self.nodes[0].zcrawjoinsplit(protect_tx, {}, {zcaddress: total_in - Decimal(0.01)}, total_in - Decimal(0.01), 0)

            receive_result = self.nodes[0].zcrawreceive(zcsecretkey, joinsplit_result["encryptednote1"])
            assert_equal(receive_result["exists"], False)

            protect_tx = self.nodes[0].signrawtransaction(joinsplit_result["rawtxn"])

            # disconnect the nodes so that the chains are not synced
            if (round == just_pre_fork_round):
                disconnect_nodes(self.nodes[0], 1)

            try:
                self.nodes[0].sendrawtransaction(protect_tx["hex"])
                if (round == post_fork_round):
                    assert(False)
            except JSONRPCException as e:
                if (round == pre_fork_round or round == just_pre_fork_round):
                    print("Unexpected exception caught during testing: " + str(sys.exc_info()[0]))
                    assert(False)
                else:
                    print("Expected exception caught during testing due to deprecation (error=" + str(e.error["code"]) + ")")
                    assert_equal(e.error["code"], RPC_VERIFY_REJECTED)
                    continue # other steps of this round are skipped

            if (round == just_pre_fork_round):
                # advance chain on node1 (so that shielded pool deprecation hard fork is active)
                self.nodes[1].generate(20)
                connect_nodes(self.nodes[0], 1)
                sync_blocks(self.nodes)
                # make sure now deprecated tx is node0 mempool
                assert_equal(self.nodes[0].getmempoolinfo()["size"], 1)
                # mine a block including now deprecated tx
                try:
                    self.nodes[0].generate(1)
                    assert(False)
                except JSONRPCException as e:
                    # failing at CreateNewBlock->TestBlockValidity
                    # (very close to block validation failure a node receiving this block would incur into)
                    print("Expected exception caught during testing due to deprecation (error=" + str(e.error["code"]) + ")")
                    self.nodes[0].clearmempool()
                    self.sync_all()
                    continue # other steps of this round are skipped

            self.nodes[0].generate(1)

            receive_result = self.nodes[0].zcrawreceive(zcsecretkey, joinsplit_result["encryptednote1"])
            assert_equal(receive_result["exists"], True)

            # The pure joinsplit we create should be mined in the next block
            # despite other transactions being in the mempool.
            addrtest = self.nodes[0].getnewaddress()
            for xx in range(0,10):
                self.nodes[0].generate(1)
                for x in range(0,50):
                    self.nodes[0].sendtoaddress(addrtest, 0.01)

            joinsplit_tx = self.nodes[0].createrawtransaction([], {})
            joinsplit_result = self.nodes[0].zcrawjoinsplit(joinsplit_tx, {receive_result["note"] : zcsecretkey}, {zcaddress: 45.73}, 0, 0.01)

            self.nodes[0].sendrawtransaction(joinsplit_result["rawtxn"])
            self.nodes[0].generate(1)

            receive_result = self.nodes[0].zcrawreceive(zcsecretkey, joinsplit_result["encryptednote1"])
            assert_equal(receive_result["exists"], True)

            if (round == pre_fork_round):
                blockcount = self.nodes[0].getblockcount()
                if (blockcount < ForkHeight - 10):
                    self.nodes[0].generate(ForkHeight - 10 - blockcount)
                    self.sync_all()


if __name__ == '__main__':
    JoinSplitTest().main()
