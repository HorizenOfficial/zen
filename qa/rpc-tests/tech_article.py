#!/usr/bin/env python3
import codecs
from decimal import Decimal
from test_framework.test_framework import BitcoinTestFramework
from test_framework.authproxy import JSONRPCException
from test_framework.util import initialize_chain_clean, start_nodes, connect_nodes_bi, \
                                sync_blocks, sync_mempools
import os
import zipfile
import time
from test_framework.test_framework import MINER_REWARD_POST_H200
from test_framework.mc_test.mc_test import *
import random
import sys

NUMB_OF_NODES = 7
FEW = 100
MANY = 1000

class TechArticle(BitcoinTestFramework):

    def create_data_for_setup(self):

        # NOTE: all sendtoaddress calls use old algorithm
        # node0: miner
        # node1, node2: few low funds
        # node3, node4: many low funds
        # node5, node6: many low, middle and high funds

        for i in range(2): # for preventing timeout when calling generate() with too many blocks
            self.nodes[0].generate(10000)
            self.sync_all()
            print("round of mining done")

        # here random values are generated for creating the initial setup data
        setup_random_values = [[], [], []]
        for i in range(3): # low [1000 zats, 100000 zats], middle [100000 zats, 10000000 zats], high [10000000 zats, 10 zens]
            file_name = ("setup_random_values_low" if i == 0 else ("setup_random_values_middle" if i == 1 else "setup_random_values_high")) + ".txt"
            from_value = 1000 * pow(10, i * 2)
            to_value = 1000 * pow(10, (i + 1) * 2)
            for j in range(1000000):
                setup_random_values[i].append(random.randint(from_value, to_value))
            with open(file_name, 'w') as f:
                f.write(str(setup_random_values[i]))
        setup_random_values_indexer = [0, 0, 0]

        def send(nodes_range, wallet_size, funds_type):
            for node in nodes_range:
                for i in range(wallet_size):
                    if funds_type == 0:
                        index = 0 # low only
                    else:
                        index = i % 3 # low, middle, high
                    if (self.nodes[0].getbalance() > setup_random_values[index][setup_random_values_indexer[index]] / 100000000):
                        self.nodes[0].sendtoaddress(self.nodes[node].listaddresses()[0], setup_random_values[index][setup_random_values_indexer[index]] / 100000000)
                    else:
                        print("insufficient funds")
                    setup_random_values_indexer[index] += 1
                    if (i % (wallet_size / 10) == 0):
                        print(f"node {node} - {str(i / (wallet_size / 10))}")
                while (len(self.nodes[0].getrawmempool()) > 0):
                    self.nodes[0].generate(1)
                print(f"done sending to node {node}")
                self.sync_all()

        # here random values are used for creating the initial setup data
        send(range(1, 3), FEW, 0)
        send(range(3, 5), MANY, 0)
        send(range(5, 7), MANY, 1)

    def create_data_for_test(self):
        # here random values are generated for being used later in the test (in order to grant randomness in a repeatable way)
        test_random_values = [[], [], []]
        for i in range(3): # low [1000 zats, 100000 zats], middle [100000 zats, 10000000 zats], high [10000000 zats, 10 zens]
            file_name = ("test_random_values_low" if i == 0 else ("test_random_values_middle" if i == 1 else "test_random_values_high")) + ".txt"
            from_value = 1000 * pow(10, i * 2)
            to_value = 1000 * pow(10, (i + 1) * 2)
            for j in range(1000000):
                test_random_values[i].append(random.randint(from_value, to_value))
            with open(file_name, 'w') as f:
                f.write(str(test_random_values[i]))

    def import_data_to_data_dir(self):

        # importing datadir resource
        #
        # 20000 blocks generated on node0 + (low fund sent from node0 to node1) * 100 +
        # 1 block generated on node0 + (low fund sent from node0 to node2) * 1000 +
        # 1 block generated on node0 + (low/middle/high fund sent from node0 to node3) * 1000 +
        # 1 block generated on node0
        #

        resource_file = os.sep.join([os.path.dirname(__file__), 'resources', 'tech_article', 'test_setup_.zip'])
        with zipfile.ZipFile(resource_file, 'r') as zip_ref:
            zip_ref.extractall(self.options.tmpdir)

    def setup_chain(self):
        self.import_data_to_data_dir()
        print("Initializing test directory " + self.options.tmpdir)
        initialize_chain_clean(self.options.tmpdir, NUMB_OF_NODES)

    def setup_network(self, split=False):
        self.nodes = start_nodes(NUMB_OF_NODES, self.options.tmpdir, extra_args=[['-debug=selectcoins', '-maxtipage=36000000']] * NUMB_OF_NODES )
        for i in range(NUMB_OF_NODES - 1):
            connect_nodes_bi(self.nodes, i, i+1)
        self.is_network_split=False
        self.sync_all()

    def print_report(self, test_mode, algorithm, indexer_offset, subtract_fee_from_amount, AVG_TIME, DELTA_UNSPENT, CHANGE_OUTPUT_COUNT, AVG_FEE_RATE, ERRORS_COUNT):
        print(f"AVG_TIME [s]: {AVG_TIME}")
        print(f"DELTA_UNSPENT [%]: {DELTA_UNSPENT}")
        print(f"CHANGE_OUTPUT_COUNT [%]: {CHANGE_OUTPUT_COUNT}")
        print(f"AVG_FEE_RATE [zen/byte]: {AVG_FEE_RATE}")
        print(f"ERRORS_COUNT [%]: {ERRORS_COUNT}")
        file_name = str(f"results_{test_mode}.csv")
        if (not os.path.exists(file_name)):
            with open(file_name, 'a') as f:
                f.write(str(f"ALGORITHM;INDEXER_OFFSET;SUBTRACT_FEE_FROM_AMOUNT;AVG_TIME;DELTA_UNSPENT;CHANGE_OUTPUT_COUNT;AVG_FEE_RATE;ERRORS_COUNT\n"))
        with open(file_name, 'a') as f:
            f.write(str(f"{algorithm};{indexer_offset};{subtract_fee_from_amount};{AVG_TIME};{DELTA_UNSPENT};{CHANGE_OUTPUT_COUNT};{AVG_FEE_RATE};{ERRORS_COUNT}\n"))
    
    def run_test (self):
        # self.create_data_for_setup()
        # self.create_data_for_test()
        # return

        test_mode = "few-low"
        algorithm = "algo-new"
        indexer_offset = 0
        subtract_fee_from_amount = False
        for arg in sys.argv:
            if (arg.startswith("test_mode=")):
                test_mode = arg.replace("test_mode=", "")
            if (arg.startswith("algorithm=")):
                algorithm = arg.replace("algorithm=", "")
            if (arg.startswith("indexer_offset=")):
                indexer_offset = int(arg.replace("indexer_offset=", ""))
            if (arg.startswith("subtract_fee_from_amount=")):
                subtract_fee_from_amount = (arg.replace("subtract_fee_from_amount=", "").upper() == "TRUE")

        if (test_mode not in ["few-low", "many-low", "many-lowmiddlehigh"] or
            algorithm not in ["algo-new", "algo-old"] or
            indexer_offset < 0 or indexer_offset > 1000000 - MANY):
            sys.exit("UNSUPPORTED")           

        # import data for test
        test_random_values = [[], [], []]
        for i in range(3): # low [1000 zats, 100000 zats], middle [100000 zats, 10000000 zats], high [10000000 zats, 10 zens]
            file_name = os.sep.join([self.options.tmpdir, ("test_random_values_low" if i == 0 else ("test_random_values_middle" if i == 1 else "test_random_values_high")) + ".txt"])
            with open(file_name, 'r') as f:
                stringed_list = f.read()[1:-1]
                test_random_values[i] = [int(s) for s in stringed_list.split(',')]
        test_random_values_indexer = [0, 0, 0]

        n0addr = self.nodes[0].listaddresses()[0]

        if (test_mode == "few-low"):
            nodes_range = range(1, 3)
            wallet_size = FEW
            funds_type = 0
        elif (test_mode == "many-low"):
            nodes_range = range(3, 5)
            wallet_size = MANY
            funds_type = 0
        elif (test_mode == "many-lowmiddlehigh"):
            nodes_range = range(5, 7)
            wallet_size = MANY
            funds_type = 1
        
        for node in nodes_range:
            test_random_values_indexer = [indexer_offset, indexer_offset, indexer_offset]

            AVG_TIME = 0            # [s]
            DELTA_UNSPENT = 0       # [%]
            CHANGE_OUTPUT_COUNT = 0 # [%]
            AVG_FEE_RATE = 0        # [zen/byte]
            ERRORS_COUNT = 0        # [%]
        
            unspent_start = self.nodes[node].listunspent(0)
            assert(len(unspent_start) == wallet_size)
            loops = int(wallet_size / 10)
            for i in range(loops):
                if funds_type == 0:
                    index = 0 # low only
                else:
                    index = i % 3 # low, middle, high
                time_start = time.time()
                try:
                    tx_id = self.nodes[node].sendtoaddress(n0addr, test_random_values[index][test_random_values_indexer[index]] / 100000000, "", "", subtract_fee_from_amount) # from zats to zens
                    AVG_TIME += time.time() - time_start
                    tx_raw = self.nodes[node].getrawtransaction(tx_id, 1)
                    if (len(tx_raw["vout"]) > 1):
                        CHANGE_OUTPUT_COUNT += 1
                    tx_info = self.nodes[node].getrawmempool(True)[tx_id]
                    AVG_FEE_RATE += tx_info["fee"] / tx_info["size"]
                    sync_mempools([self.nodes[0], self.nodes[node]])
                    self.nodes[0].generate(1)
                    sync_blocks([self.nodes[0], self.nodes[node]])
                except JSONRPCException as e:
                    print(f"FAIL {e.error}")
                    ERRORS_COUNT += 1
                test_random_values_indexer[index] += 1
            unspent_end = self.nodes[node].listunspent(0)       
            AVG_TIME /= (loops - ERRORS_COUNT)
            DELTA_UNSPENT = (len(unspent_end) - len(unspent_start)) / len(unspent_start) * 100
            CHANGE_OUTPUT_COUNT = CHANGE_OUTPUT_COUNT / (loops - ERRORS_COUNT) * 100
            AVG_FEE_RATE /= (loops - ERRORS_COUNT)
            ERRORS_COUNT = ERRORS_COUNT / loops * 100
            self.print_report(test_mode, algorithm, indexer_offset, subtract_fee_from_amount, AVG_TIME, DELTA_UNSPENT, CHANGE_OUTPUT_COUNT, AVG_FEE_RATE, ERRORS_COUNT)

        return


if __name__ == '__main__':
        TechArticle().main()