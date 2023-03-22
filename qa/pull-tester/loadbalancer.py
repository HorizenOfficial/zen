#!/usr/bin/env python3
import sys

workers = int(sys.argv[1])
selectedChunk = int(sys.argv[2])        # bash style, they go from 1 to workers

# Restrict selectedChunk value in [1,workers] (no silent fail)
selectedChunk = min(selectedChunk, workers)
selectedChunk = max(selectedChunk, 1)

# Parse the list of tests from argv and arrange them in a list of (filename, weight) tuples
tuplelist = []
for i in range(3, len(sys.argv)):
    tuple = sys.argv[i].split(',')
    tuplelist.append((tuple[0], int(tuple[1])))

## Longest-processing-time-first (LPT) algorithm
work = [[] for _ in range(workers)]
workloads = [0 for _ in range(workers)]

# 1. Order the jobs by descending order of their processing-time, such that the job with the longest processing time is first.
sortedlist = sorted(tuplelist, key = lambda x : x[1], reverse = True)

# 2. Schedule each job in this sequence into a machine in which the current load (= total processing-time of scheduled jobs) is smallest.
for i in range(len(sortedlist)):
    # find the minimum workload
    minIdx = min(range(len(workloads)), key=workloads.__getitem__)
    work[minIdx].append(sortedlist[i])
    workloads[minIdx] += sortedlist[i][1]

## outstring: "paymentdisclosure.py|txn_doublespend.py --mineblock|rawtransactions.py"
outstring = '|'.join([x[0] for x in work[selectedChunk - 1]])
print(outstring)

# rpc-tests.sh is configured to temporarely use the pipe symbol '|' as delimiter, instead the usual space ' ',
# when parsing what returned by this script.
# This procedure is done to correctly manage test names like 'txn_doublespend.py --mineblock'.
 