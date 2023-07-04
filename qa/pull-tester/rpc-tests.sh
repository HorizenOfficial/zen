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
  'paymentdisclosure.py',99,332
  'prioritisetransaction.py',47,145
  'wallet_treestate.py',136,345
  'wallet_protectcoinbase.py',294,1368
  'wallet_shieldcoinbase.py',214,970
  'wallet_mergetoaddress.py',491,1740
  'wallet_mergetoaddress_2.py',975,2539
  'wallet.py',131,692
  'wallet_nullifiers.py',109,372
  'wallet_1941.py',53,170
  'wallet_grothtx.py',91,262
  'listtransactions.py',129,241
  'mempool_resurrect_test.py',6,16
  'txn_doublespend.py',22,139
  'txn_doublespend.py --mineblock',23,62
  'getchaintips.py',66,269
  'rawtransactions.py',141,397
  'rest.py',26,59
  'mempool_spendcoinbase.py',6,96
  'mempool_coinbase_spends.py',14,34
  'mempool_tx_input_limit.py',91,308
  'httpbasics.py',21,63
  'zapwallettxes.py',35,86
  'proxy_test.py',22,142
  'merkle_blocks.py',69,163
  'fundrawtransaction.py',60,128
  'signrawtransactions.py',6,15
  'walletbackup.py',432,1478
  'key_import_export.py',41,86
  'nodehandling.py',428,666
  'reindex.py',13,33
  'decodescript.py',6,16
  'disablewallet.py',6,16
  'zcjoinsplit.py',45,215
  'zcjoinsplitdoublespend.py',199,733
  'zkey_import_export.py',381,1588
  'getblocktemplate.py',12,35
  'bip65-cltv-p2p.py',6,18
  'bipdersig-p2p.py',7,17
  'nulldata.py',38,69
  'blockdelay.py',46,101
  'blockdelay_2.py',51,84
  'z_sendmany.py',71,196
  'sc_create.py',185,707
  'sc_split.py',38,84
  'sc_invalidate.py',41,99
  'sc_cert_base.py',104,289
  'sc_cert_nonceasing.py',97,304
  'sc_cert_fee.py',53,144
  'sc_cert_epoch.py',87,224
  'sc_cert_invalidate.py',58,131
  'sc_fwd_maturity.py',54,129
  'sc_rawcertificate.py',74,186
  'getunconfirmedtxdata.py',46,108
  'sc_cr_and_fw_in_mempool.py',57,137
  'sc_cert_change.py',55,145
  'sc_cert_orphans.py',59,145
  'sc_cert_maturity.py',56,144
  'sbh_rpc_cmds.py',41,101
  'sc_cert_ceasing.py',67,179
  'sc_cert_customfields.py',106,317
  'sc_cert_getraw.py',48,114
  'sc_quality_mempool.py',109,334
  'sc_ft_and_mbtr_fees.py',47,91
  'sc_ft_and_mbtr_fees_update.py',305,1131
  'sc_bwt_request.py',70,165
  'sc_cert_quality_wallet.py',101,250
  'ws_messages.py',71,173
  'ws_getsidechainversions.py',47,138
  'sc_cert_ceasing_split.py',66,161
  'sc_async_proof_verifier.py',97,227
  'sc_quality_blockchain.py',86,254
  'sc_quality_voiding.py',57,144
  'sc_csw_actcertdata.py',104,315
  'sc_csw_actcertdata_null.py',54,136
  'sc_cert_ceasing_sg.py',54,138
  'sc_csw_nullifier.py',120,319
  'sc_getscinfo.py',121,592
  'sc_quality_nodes.py',51,126
  'sc_cert_memcleanup_split.py',69,173
  'sc_csw_fundrawtransaction.py',93,260
  'sc_proof_verifier_low_priority_threads.py',49,82
  'subsidyhalving.py',195,361
  'cbh_rpfix.py',45,113
  'cbh_rpcheck.py',26,61
  'tlsprotocols.py',12,34
  'mempool_double_spend.py',21,60
  'getblockmerkleroots.py',67,156
  'sc_block_partitions.py',60,153
  'sc_cert_bwt_amount_rounding.py',30,73
  'sc_csw_eviction_from_mempool.py',124,377
  'sc_csw_memcleanup_split.py',70,188
  'sc_csw_balance_exceeding.py',57,162
  'sc_stale_ft_and_mbtr.py',121,293
  'sc_cert_getblocktemplate.py',253,993
  'sc_cert_bt_immature_balances.py',40,94
  'sc_rpc_cmds_fee_handling.py',137,254
  'sc_cert_listsinceblock.py',70,158
  'sc_cert_dust.py',98,236
  'sc_keyrot.py',47,149
  'txindex.py',28,72
  'addressindex.py',34,95
  'spentindex.py',18,74
  'timestampindex.py',21,75
  'sc_cert_addressindex.py',128,508
  'sc_cert_addrmempool.py',46,168
  'getblockexpanded.py',191,478
  'sc_rpc_cmds_json_output.py',68,187
  'sc_version.py',104,371
  'sc_getscgenesisinfo.py',86,286
  'fundaddresses.py',12,25
  'sc_getcertmaturityinfo.py',68,231
  'sc_big_commitment_tree.py',63,110
  'sc_big_commitment_tree_getblockmerkleroot.py',11,25
  'p2p_ignore_spent_tx.py',215,455
  'shieldedpooldeprecation_rpc.py',558,1794
  'mempool_size_limit.py',121,203
);

testScriptsExt=(
  'getblocktemplate_longpoll.py',120,207
  'getblocktemplate_proposals.py',57,129
  'getblocktemplate_blockmaxcomplexity.py',55,136
  'getblocktemplate_priority.py',39,84
  # 'pruning.py'                # disabled for Zen. Failed because of the issue #1302 in zcash
  'forknotify.py',27,60
  # 'hardforkdetection.py'      # disabled for Zen. Failed because of the issue #1302 in zcash
  # 'invalidateblock.py'        # disabled for Zen. Failed because of the issue #1302 in zcash
  'keypool.py',12,34
  'receivedby.py',30,68
  'rpcbind_test.py',60,140
  #  'script_test.py'
  'smartfees.py',158,480
  'maxblocksinflight.py',14,25
  'invalidblockrequest.py',40,100
  'invalidblockposthalving.py',113,250
  'p2p-acceptblock.py',202,450
  'replay_protection.py',22,56
  'headers_01.py',14,34
  'headers_02.py',22,52
  'headers_03.py',22,54
  'headers_04.py',26,57
  'headers_05.py',48,82
  'headers_06.py',44,87
  'headers_07.py',107,228
  'headers_08.py',25,59
  'headers_09.py',44,81
  'headers_10.py',36,72
  'checkblockatheight.py',103,236
  'sc_big_block.py',92,247
);

if [ "x$ENABLE_ZMQ" = "x1" ]; then
  testScripts+=('zmq_test.py',25,73)
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
