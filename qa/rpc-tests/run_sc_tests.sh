#!/usr/bin/env bash

rm /tmp/result.log

for i in sbh_rpc_cmds.py rawtransactions.py getblocktemplate_proposals.py listtransactions.py ws_messages.py sc_*py
do
	echo "$i ---------------------------" | tee -a /tmp/result.log
	time ./$i | tee -a /tmp/result.log
	echo $? | tee -a /tmp/result.log
	echo | tee -a /tmp/result.log
done
