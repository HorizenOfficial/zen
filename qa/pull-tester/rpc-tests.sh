#!/bin/bash
set -e -o pipefail

CURDIR=$(cd $(dirname "$0"); pwd)
# Get BUILDDIR and REAL_BITCOIND
. "${CURDIR}/tests-config.sh"

export BITCOINCLI="${BUILDDIR}/qa/pull-tester/run-bitcoin-cli"
export BITCOIND="${REAL_BITCOIND}"
export ZENDOOMC="${REAL_ZENDOO_MC_TEST}"

MACREBALANCE="false"

# parse args
for i in "$@"; do
  case "${i}" in
    -extended)
      EXTENDED="true"
      shift
      ;;
    -exclude=*)
      EXCLUDE="${i#*=}"
      shift
      ;;
    -split=*)
      SPLIT="${i#*=}"
      shift
      ;;
    -macrebalance)
      MACREBALANCE="true"
      shift
      ;;
    -coverage)
      COVERAGE="true"
      shift
      ;;
    *)
      # unknown option/passOn
      passOn+="${i} "
      ;;
  esac
done

#Run the tests

# When adding new tests, do NOT introduce spaces between the filename, comma separator, and weight!
# Also, note that comma ',' is used as delimiter. Please modify loadbalancer.py if in the future we
# need to use this character in the filename field.
testScripts=(
  'paymentdisclosure.py',93,294
  'prioritisetransaction.py',69,121
  'wallet_treestate.py',126,315
  'wallet_protectcoinbase.py',378,1496
  'wallet_shieldcoinbase.py',197,881
  'wallet_mergetoaddress.py',473,1508
  'wallet_mergetoaddress_2.py',1010,2865
  'wallet.py',109,615
  'wallet_nullifiers.py',127,325
  'wallet_1941.py',49,152
  'wallet_grothtx.py',82,234
  'listtransactions.py',56,189
  'mempool_resurrect_test.py',6,83
  'txn_doublespend.py',22,62
  'txn_doublespend.py --mineblock',49,129
  'getchaintips.py',67,185
  'rawtransactions.py',111,340
  'rest.py',20,53
  'mempool_spendcoinbase.py',6,17
  'mempool_coinbase_spends.py',16,103
  'mempool_tx_input_limit.py',86,279
  'httpbasics.py',22,62
  'zapwallettxes.py',30,74
  'proxy_test.py',22,129
  'merkle_blocks.py',55,149
  'fundrawtransaction.py',43,109
  'signrawtransactions.py',6,15
  'walletbackup.py',397,1387
  'key_import_export.py',29,66
  'nodehandling.py',353,514
  'reindex.py',12,30
  'decodescript.py',6,15
  'disablewallet.py',6,15
  'zcjoinsplit.py',96,235
  'zcjoinsplitdoublespend.py',189,819
  'zkey_import_export.py',365,1433
  'getblocktemplate.py',11,31
  'bip65-cltv-p2p.py',6,16
  'bipdersig-p2p.py',6,16
  'nulldata.py',24,57
  'blockdelay.py',36,88
  'blockdelay_2.py',42,75
  'z_sendmany.py',62,171
  'sc_create.py',145,624
  'sc_split.py',28,70
  'sc_invalidate.py',32,87
  'sc_cert_base.py',76,265
  'sc_cert_nonceasing.py',81,270
  'sc_cert_fee.py',42,119
  'sc_cert_epoch.py',72,184
  'sc_cert_invalidate.py',41,99
  'sc_fwd_maturity.py',41,116
  'sc_rawcertificate.py',56,174
  'getunconfirmedtxdata.py',40,97
  'sc_cr_and_fw_in_mempool.py',47,117
  'sc_cert_change.py',44,128
  'sc_cert_orphans.py',47,123
  'sc_cert_maturity.py',43,124
  'sbh_rpc_cmds.py',31,87
  'sc_cert_ceasing.py',62,249
  'sc_cert_customfields.py',85,268
  'sc_cert_getraw.py',33,97
  'sc_quality_mempool.py',85,283
  'sc_ft_and_mbtr_fees.py',35,72
  'sc_ft_and_mbtr_fees_update.py',230,1006
  'sc_bwt_request.py',51,143
  'sc_cert_quality_wallet.py',82,224
  'ws_messages.py',51,147
  'ws_getsidechainversions.py',36,123
  'sc_cert_ceasing_split.py',51,144
  'sc_async_proof_verifier.py',77,201
  'sc_quality_blockchain.py',67,223
  'sc_quality_voiding.py',44,123
  'sc_csw_actcertdata.py',86,283
  'sc_csw_actcertdata_null.py',41,122
  'sc_cert_ceasing_sg.py',41,121
  'sc_csw_nullifier.py',96,293
  'sc_getscinfo.py',105,580
  'sc_quality_nodes.py',35,103
  'sc_cert_memcleanup_split.py',52,150
  'sc_csw_fundrawtransaction.py',73,220
  'sc_proof_verifier_low_priority_threads.py',45,75
  'subsidyhalving.py',92,177
  'cbh_rpfix.py',38,99
  'cbh_rpcheck.py',20,53
  'tlsprotocols.py',12,32
  'mempool_double_spend.py',20,52
  'getblockmerkleroots.py',58,200
  'sc_block_partitions.py',44,136
  'sc_cert_bwt_amount_rounding.py',23,63
  'sc_csw_eviction_from_mempool.py',95,420
  'sc_csw_memcleanup_split.py',55,167
  'sc_csw_balance_exceeding.py',44,142
  'sc_stale_ft_and_mbtr.py',93,263
  'sc_cert_getblocktemplate.py',150,859
  'sc_cert_bt_immature_balances.py',27,81
  'sc_rpc_cmds_fee_handling.py',114,223
  'sc_cert_listsinceblock.py',54,143
  'sc_cert_dust.py',82,214
  'sc_keyrot.py',37,142
  'txindex.py',24,65
  'addressindex.py',41,89
  'spentindex.py',25,63
  'timestampindex.py',28,69
  'sc_cert_addressindex.py',138,713
  'sc_cert_addrmempool.py',50,135
  'getblockexpanded.py',141,430
  'sc_rpc_cmds_json_output.py',51,162
  'sc_version.py',85,367
  'sc_getscgenesisinfo.py',70,248
  'fundaddresses.py',11,20
  'sc_getcertmaturityinfo.py',55,205
  'sc_big_commitment_tree.py',52,96
  'sc_big_commitment_tree_getblockmerkleroot.py',9,19
  'p2p_ignore_spent_tx.py',211,480
  'shieldedpooldeprecation_rpc.py',526,1682
  'mempool_size_limit.py',111,172
  'mempool_size_limit_more.py',39,53
  'mempool_size_limit_even_more.py',103,220
  'mempool_hard_fork_cleaning.py',44,112
);

testScriptsExt=(
  'getblocktemplate_longpoll.py',113,126
  'getblocktemplate_proposals.py',49,121
  'getblocktemplate_blockmaxcomplexity.py',51,130
  'getblocktemplate_priority.py',35,75
  # 'pruning.py'                # disabled for Zen. Failed because of the issue #1302 in zcash
  'forknotify.py',22,48
  # 'hardforkdetection.py'      # disabled for Zen. Failed because of the issue #1302 in zcash
  # 'invalidateblock.py'        # disabled for Zen. Failed because of the issue #1302 in zcash
  'keypool.py',12,29
  'receivedby.py',25,66
  'rpcbind_test.py',60,0
  #  'script_test.py'
  'smartfees.py',198,322
  'maxblocksinflight.py',14,23
  'invalidblockrequest.py',13,0
  'invalidblockposthalving.py',27,0
  'p2p-acceptblock.py',45,0
  'replay_protection.py',19,49
  'headers_01.py',11,31
  'headers_02.py',17,46
  'headers_03.py',17,48
  'headers_04.py',22,52
  'headers_05.py',41,72
  'headers_06.py',42,82
  'headers_07.py',39,80
  'headers_08.py',21,53
  'headers_09.py',38,69
  'headers_10.py',22,53
  'checkblockatheight.py',76,201
  'sc_big_block.py',71,222
);

if [ "x$ENABLE_ZMQ" = "x1" ]; then
  testScripts+=('zmq_test.py',23,65)
fi

if [ "x$ENABLE_PROTON" = "x1" ]; then
  testScripts+=('proton_test.py',100,200)
fi

# include extended tests
if [ ! -z "$EXTENDED" ] && [ "${EXTENDED}" = "true" ]; then
  testScripts+=( "${testScriptsExt[@]}" )
fi

# remove tests provided by --exclude= from testScripts
if [ ! -z "$EXCLUDE" ]; then
  for target in ${EXCLUDE//,/ }; do
    for i in "${!testScripts[@]}"; do
      if [[ "${testScripts[i]}" == *"$target"* ]]; then
        unset "testScripts[i]"
      fi
    done
  done
fi

# split array into m parts and only run tests of part n where SPLIT=m:n
if [ ! -z "$SPLIT" ]; then
  chunks="${SPLIT%*:*}"
  chunk="${SPLIT#*:}"
else
  chunks='1'
  chunk='1'
fi

# call the load balancer script and save the result into a '|'-delimited string
testList=$(${BUILDDIR}/qa/pull-tester/loadbalancer.py "${chunks}" "${chunk}" "${MACREBALANCE}" "${testScripts[@]}")

# convert back the load balancer output into an array. Spaces in filename are preserved
originalIFS="$IFS"; IFS='|'
testScripts=( ${testList} )
IFS="$originalIFS"

successCount=0
declare -a failures

function checkFileExists
{
  # take only file name, strip off any options/param
  local TestScriptFile="$(echo $1 | cut -d' ' -f1)"
  local TestScriptFileAndPath="${BUILDDIR}/qa/rpc-tests/${TestScriptFile}"

  if [ ! -f "${TestScriptFileAndPath}" ]; then
    echo -e "\nWARNING: file not found [ ${TestScriptFileAndPath} ]"
    failures[${#failures[@]}]="(#-NotFound-$1-#)"
  fi
}


function runTestScript
{
  local testName="$1"
  shift

  echo -e "=== Running testscript ${testName} ==="

  start=`date +%s`
  if eval "$@"; then
    end=`date +%s`
    runtime=$((end-start))
    successCount=$(expr $successCount + 1)
    echo "--- Success: ${testName} - elapsed time: ${runtime} ---"
  else
    failures[${#failures[@]}]="$testName"
    echo "!!! FAIL: ${testName} !!!"
  fi
}

if [ "x${ENABLE_BITCOIND}${ENABLE_UTILS}${ENABLE_WALLET}" = "x111" ]; then
  # Create baseline coverage data
  if [ ! -z "$COVERAGE" ] && [ "${COVERAGE}" = "true" ];
  then
    lcov --directory "${BUILDDIR}"/src --zerocounters
    lcov -c -i -d "${BUILDDIR}/src" -o py_test_coverage_base.info -rc lcov_branch_coverage=1
    lcov -r py_test_coverage_base.info "/usr/include/*" \
                      "*/depends/x86_64-unknown-linux-gnu/include/*.h" \
                      "*/depends/x86_64-unknown-linux-gnu/include/boost/*" \
                      "*/depends/x86_64-unknown-linux-gnu/include/gmock/*" \
                      "*/depends/x86_64-unknown-linux-gnu/include/gtest/*" \
                      "*/depends/x86_64-linux-gnu/include/*.h" \
                      "*/depends/x86_64-linux-gnu/include/boost/*" \
                      "*/depends/x86_64-linux-gnu/include/gmock/*" \
                      "*/depends/x86_64-linux-gnu/include/gtest/*" \
                      "*/src/gtest/*" \
                      "*/src/test/*" \
                      "*/src/wallet/gtest/*" \
                      "*/src/wallet/test/*" \
                      -o py_test_coverage_base_filtered.info
  fi

  for (( i = 0; i < ${#testScripts[@]}; i++ )); do
    checkFileExists "${testScripts[$i]}"

    if [ -z "$1" ] || [ "${1:0:1}" = "-" ] || [ "$1" = "${testScripts[$i]}" ] || [ "$1.py" = "${testScripts[$i]}" ]; then
      runTestScript \
        "${testScripts[$i]}" \
        "${BUILDDIR}/qa/rpc-tests/${testScripts[$i]}" \
        --srcdir "${BUILDDIR}/src" ${passOn}
    fi
  done

  # Evaluate and aggregate the coverage reports and send everything to Codacy
  if [ ! -z "$COVERAGE" ] && [ "${COVERAGE}" = "true" ];
  then
    lcov -c -d "${BUILDDIR}/src" -o py_test_coverage_after.info -rc lcov_branch_coverage=1
    lcov -r py_test_coverage_after.info "/usr/include/*" \
        "*/depends/x86_64-unknown-linux-gnu/include/*.h" \
        "*/depends/x86_64-unknown-linux-gnu/include/boost/*" \
        "*/depends/x86_64-unknown-linux-gnu/include/gmock/*" \
        "*/depends/x86_64-unknown-linux-gnu/include/gtest/*" \
        "*/depends/x86_64-linux-gnu/include/*.h" \
        "*/depends/x86_64-linux-gnu/include/boost/*" \
        "*/depends/x86_64-linux-gnu/include/gmock/*" \
        "*/depends/x86_64-linux-gnu/include/gtest/*" \
        "*/src/gtest/*" \
        "*/src/test/*" \
        "*/src/wallet/gtest/*" \
        "*/src/wallet/test/*" \
        -o py_test_coverage_after_filtered.info
    lcov -a py_test_coverage_base_filtered.info -a py_test_coverage_after_filtered.info -o py_test_coverage_"${chunk}".info
    COMMIT=$(git log -1 --format="%H")
    bash <(curl -Ls https://coverage.codacy.com/get.sh) report --partial -l CPP --commit-uuid "${COMMIT}" \
        --project-token ${CODACY_PROJECT_TOKEN} -r py_test_coverage_"${chunk}".info
    bash <(curl -Ls https://coverage.codacy.com/get.sh) report --partial -l C   --commit-uuid "${COMMIT}" \
        --project-token ${CODACY_PROJECT_TOKEN} -r py_test_coverage_"${chunk}".info
  fi

  total=$(($successCount + ${#failures[@]}))
  echo -e "\n\nTests completed: $total"
  echo "successes $successCount; failures: ${#failures[@]}"

  if [ $total -eq 0 ]; then
    echo -e "\nCould not exec any test: File name [$1]"
    checkFileExists $1
    exit 1
  fi

  if [ ${#failures[@]} -gt 0 ]; then
    echo -e "\nFailing tests: ${failures[*]}"
    exit 1
  else
    exit 0
  fi
else
  echo "No rpc tests to run. Wallet, utils, and bitcoind must all be enabled"
fi
