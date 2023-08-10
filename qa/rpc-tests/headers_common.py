#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Copyright (c) 2018 The Zencash developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.util import mark_logs, assert_equal, colorize as cc

def get_block_finality(nodes, idx, bl, expected_val, DEBUG_MODE = 1):
    nodefin = nodes[idx].getblockfinalityindex(bl)
    assert_equal(nodefin, expected_val)
    mark_logs(cc("g", f"  Node{idx} has: ") + str(nodefin), nodes, DEBUG_MODE, color='n')

def dump_ordered_tips(tip_list):
    sorted_x = sorted(tip_list, key=lambda k: k['status'])
    c = 0
    for y in sorted_x:
        if (c == 0):
            print(y)
        else:
            print(" ",y)
        c = 1

def print_ordered_tips(nodes):
    print()
    print(cc("e", "Printing nodes' tips"))
    for i in range(len(nodes)):
        dump_ordered_tips(nodes[i].getchaintips())
        print("---")
    print()