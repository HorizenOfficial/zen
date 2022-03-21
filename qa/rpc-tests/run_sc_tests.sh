#!/usr/bin/env bash

set -o pipefail

rm /tmp/result.log

for i in sbh_rpc_cmds.py rawtransactions.py getblocktemplate_proposals.py listtransactions.py ws_messages.py ws_getsidechainversions.py sc_*py
do
	echo "$i ---------------------------" | tee -a /tmp/result.log
	time stdbuf --output=L python2 ./$i 2>&1 | tee -a /tmp/result.log
	echo $? | tee -a /tmp/result.log
	echo | tee -a /tmp/result.log
done
