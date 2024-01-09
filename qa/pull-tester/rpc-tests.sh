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
  'paymentdisclosure.py',102,313
  'prioritisetransaction.py',79,140
  'wallet_treestate.py',133,308
  'wallet_protectcoinbase.py',406,1636
  'wallet_shieldcoinbase.py',216,898
  'wallet_mergetoaddress.py',502,1661
  'wallet_mergetoaddress_2.py',1004,2978
  'wallet.py',130,765
  'wallet_nullifiers.py',123,340
  'wallet_1941.py',54,167
  'wallet_grothtx.py',90,258
  'listtransactions.py',125,224
  'mempool_resurrect_test.py',6,15
  'txn_doublespend.py',23,65
  'txn_doublespend.py --mineblock',24,63
  'getchaintips.py',85,262
  'rawtransactions.py',138,329
  'rest.py',27,58
  'mempool_spendcoinbase.py',6,15
  'mempool_coinbase_spends.py',16,37
  'mempool_tx_input_limit.py',93,289
  'httpbasics.py',22,134
  'zapwallettxes.py',34,84
  'proxy_test.py',22,136
  'merkle_blocks.py',63,153
  'fundrawtransaction.py',59,124
  'signrawtransactions.py',6,16
  'walletbackup.py',426,1445
  'key_import_export.py',38,80
  'nodehandling.py',360,529
  'reindex.py',12,31
  'decodescript.py',6,16
  'disablewallet.py',6,15
  'zcjoinsplit.py',48,134
  'zcjoinsplitdoublespend.py',197,860
  'zkey_import_export.py',384,1552
  'getblocktemplate.py',13,32
  'bip65-cltv-p2p.py',6,18
  'bipdersig-p2p.py',6,17
  'nulldata.py',38,68
  'blockdelay.py',46,97
  'blockdelay_2.py',50,82
  'z_sendmany.py',71,193
  'sc_create.py',186,693
  'sc_split.py',39,79
  'sc_invalidate.py',43,93
  'sc_cert_base.py',95,274
  'sc_cert_nonceasing.py',88,277
  'sc_cert_fee.py',54,127
  'sc_cert_epoch.py',87,210
  'sc_cert_invalidate.py',60,125
  'sc_fwd_maturity.py',53,122
  'sc_rawcertificate.py',71,182
  'getunconfirmedtxdata.py',46,106
  'sc_cr_and_fw_in_mempool.py',57,128
  'sc_cert_change.py',56,138
  'sc_cert_orphans.py',59,143
  'sc_cert_maturity.py',58,136
  'sbh_rpc_cmds.py',42,92
  'sc_cert_ceasing.py',66,176
  'sc_cert_customfields.py',99,286
  'sc_cert_getraw.py',47,108
  'sc_quality_mempool.py',105,309
  'sc_ft_and_mbtr_fees.py',46,86
  'sc_ft_and_mbtr_fees_update.py',305,1094
  'sc_bwt_request.py',69,158
  'sc_cert_quality_wallet.py',96,234
  'ws_messages.py',75,160
  'ws_getsidechainversions.py',44,129
  'sc_cert_ceasing_split.py',63,157
  'sc_async_proof_verifier.py',90,211
  'sc_quality_blockchain.py',82,231
  'sc_quality_voiding.py',56,132
  'sc_csw_actcertdata.py',98,293
  'sc_csw_actcertdata_null.py',52,130
  'sc_cert_ceasing_sg.py',53,133
  'sc_csw_nullifier.py',113,310
  'sc_getscinfo.py',114,582
  'sc_quality_nodes.py',47,116
  'sc_cert_memcleanup_split.py',67,162
  'sc_csw_fundrawtransaction.py',86,248
  'sc_proof_verifier_low_priority_threads.py',50,77
  'subsidyhalving.py',225,318
  'cbh_rpfix.py',45,106
  'cbh_rpcheck.py',27,56
  'tlsprotocols.py',12,33
  'mempool_double_spend.py',22,58
  'getblockmerkleroots.py',89,152
  'sc_block_partitions.py',59,152
  'sc_cert_bwt_amount_rounding.py',31,69
  'sc_csw_eviction_from_mempool.py',111,345
  'sc_csw_memcleanup_split.py',68,171
  'sc_csw_balance_exceeding.py',56,146
  'sc_stale_ft_and_mbtr.py',120,272
  'sc_cert_getblocktemplate.py',180,880
  'sc_cert_bt_immature_balances.py',39,88
  'sc_rpc_cmds_fee_handling.py',136,254
  'sc_cert_listsinceblock.py',71,148
  'sc_cert_dust.py',97,225
  'sc_keyrot.py',44,141
  'txindex.py',25,67
  'addressindex.py',45,88
  'spentindex.py',27,69
  'timestampindex.py',30,71
  'sc_cert_addressindex.py',185,485
  'sc_cert_addrmempool.py',67,158
  'getblockexpanded.py',190,391
  'sc_rpc_cmds_json_output.py',66,176
  'sc_version.py',93,340
  'sc_getscgenesisinfo.py',80,258
  'fundaddresses.py',24,31
  'sc_getcertmaturityinfo.py',62,210
  'sc_big_commitment_tree.py',62,109
  'sc_big_commitment_tree_getblockmerkleroot.py',14,23
  'p2p_ignore_spent_tx.py',218,421
  'shieldedpooldeprecation.py',535,1309
  'mempool_size_limit.py',111,181
  'mempool_size_limit_more.py',38,62
  'mempool_size_limit_even_more.py',106,224
  'mempool_hard_fork_cleaning.py',156,344
  'shieldedpoolremoval.py',377,855
);

testScriptsExt=(
  'getblocktemplate_longpoll.py',87,126
  'getblocktemplate_proposals.py',56,126
  'getblocktemplate_blockmaxcomplexity.py',53,135
  'getblocktemplate_priority.py',39,80
  # 'pruning.py'                # disabled for Zen. Failed because of the issue #1302 in zcash
  'forknotify.py',26,55
  # 'hardforkdetection.py'      # disabled for Zen. Failed because of the issue #1302 in zcash
  # 'invalidateblock.py'        # disabled for Zen. Failed because of the issue #1302 in zcash
  'keypool.py',12,32
  'receivedby.py',28,144
  'rpcbind_test.py',60,0
  #  'script_test.py'
  'smartfees.py',174,437
  'maxblocksinflight.py',14,23
  'invalidblockrequest.py',46,0
  'invalidblockposthalving.py',116,0
  'p2p-acceptblock.py',210,0
  'replay_protection.py',23,52
  'headers_01.py',12,31
  'headers_02.py',19,47
  'headers_03.py',20,51
  'headers_04.py',24,54
  'headers_05.py',43,74
  'headers_06.py',42,84
  'headers_07.py',41,85
  'headers_08.py',26,57
  'headers_09.py',46,76
  'headers_10.py',27,56
  'checkblockatheight.py',95,213
  'sc_big_block.py',87,234
);

if [ "x$ENABLE_ZMQ" = "x1" ]; then
  testScripts+=('zmq_test.py',25,67)
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
