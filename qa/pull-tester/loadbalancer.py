#!/usr/bin/env python3
import sys

workers = int(sys.argv[1])
selectedChunk = int(sys.argv[2])        # bash style, they go from 1 to workers

# Restrict selectedChunk value in [1,workers] (no silent fail)
selectedChunk = min(selectedChunk, workers)
selectedChunk = max(selectedChunk, 1)

## STEP-BY-STEP example
## sys.argv:
## ['prova1.py', '4', '1', 'paymentdisclosure.py,63', 'txn_doublespend.py,15', 'txn_doublespend.py', '--mineblock,16', 'getchaintips.py,48', 'rawtransactions.py,101']

## join the parameter list and get a single string
## newstr: paymentdisclosure.py,63 txn_doublespend.py,15 txn_doublespend.py --mineblock,16 getchaintips.py,48 rawtransactions.py,101
newstr = ' '.join(sys.argv[3:])

## divide the string, using comma (',') as separator
## split1str: ['paymentdisclosure.py', '63 txn_doublespend.py', '15 txn_doublespend.py --mineblock', '16 getchaintips.py', '48 rawtransactions.py', '101']
split1str = newstr.split(',')

## A tuple is made by all the chars following the number of the n-th element in split1str, and the number of the n+1-th element
## tuplelist: [('paymentdisclosure.py', 63), ('txn_doublespend.py', 15), ('txn_doublespend.py --mineblock', 16), ('getchaintips.py', 48), ('rawtransactions.py', 101)]
tuplelist = [(split1str[0], int(split1str[1].split(' ')[0]))]
for i in range(1, len(split1str)-1):
    name = ' '.join(split1str[i].split(' ')[1:])
    numb = int(split1str[i+1].split(' ')[0])
    tuplelist.append((name, numb))

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
 