#!/bin/bash
set -e -o pipefail

CURDIR=$(cd $(dirname "$0"); pwd)
# Get BUILDDIR and REAL_BITCOIND
. "${CURDIR}/tests-config.sh"

export BITCOINCLI="${BUILDDIR}/qa/pull-tester/run-bitcoin-cli"
export BITCOIND="${REAL_BITCOIND}"
export ZENDOOMC="${REAL_ZENDOO_MC_TEST}"

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
  'paymentdisclosure.py',99
  'prioritisetransaction.py',47
  'wallet_treestate.py',136
  'wallet_protectcoinbase.py',294
  'wallet_shieldcoinbase.py',214
  'wallet_mergetoaddress.py',491
  'wallet_mergetoaddress_2.py',975
  'wallet.py',131
  'wallet_nullifiers.py',109
  'wallet_1941.py',53
  'wallet_grothtx.py',91
  'listtransactions.py',129
  'mempool_resurrect_test.py',6
  'txn_doublespend.py',22
  'txn_doublespend.py --mineblock',23
  'getchaintips.py',66
  'rawtransactions.py',141
  'rest.py',26
  'mempool_spendcoinbase.py',6
  'mempool_coinbase_spends.py',14
  'mempool_tx_input_limit.py',91
  'httpbasics.py',21
  'zapwallettxes.py',35
  'proxy_test.py',22
  'merkle_blocks.py',69
  'fundrawtransaction.py',60
  'signrawtransactions.py',6
  'walletbackup.py',432
  'key_import_export.py',41
  'nodehandling.py',428
  'reindex.py',13
  'decodescript.py',6
  'disablewallet.py',6
  'zcjoinsplit.py',45
  'zcjoinsplitdoublespend.py',199
  'zkey_import_export.py',381
  'getblocktemplate.py',12
  'bip65-cltv-p2p.py',6
  'bipdersig-p2p.py',7
  'nulldata.py',38
  'blockdelay.py',46
  'blockdelay_2.py',51
  'z_sendmany.py',71
  'sc_create.py',185
  'sc_split.py',38
  'sc_invalidate.py',41
  'sc_cert_base.py',104
  'sc_cert_nonceasing.py',97
  'sc_cert_fee.py',53
  'sc_cert_epoch.py',87
  'sc_cert_invalidate.py',58
  'sc_fwd_maturity.py',54
  'sc_rawcertificate.py',74
  'getunconfirmedtxdata.py',46
  'sc_cr_and_fw_in_mempool.py',57
  'sc_cert_change.py',55
  'sc_cert_orphans.py',59
  'sc_cert_maturity.py',56
  'sbh_rpc_cmds.py',41
  'sc_cert_ceasing.py',67
  'sc_cert_customfields.py',106
  'sc_cert_getraw.py',48
  'sc_quality_mempool.py',109
  'sc_ft_and_mbtr_fees.py',47
  'sc_ft_and_mbtr_fees_update.py',305
  'sc_bwt_request.py',70
  'sc_cert_quality_wallet.py',101
  'ws_messages.py',71
  'ws_getsidechainversions.py',47
  'sc_cert_ceasing_split.py',66
  'sc_async_proof_verifier.py',97
  'sc_quality_blockchain.py',86
  'sc_quality_voiding.py',57
  'sc_csw_actcertdata.py',104
  'sc_csw_actcertdata_null.py',54
  'sc_cert_ceasing_sg.py',54
  'sc_csw_nullifier.py',120
  'sc_getscinfo.py',121
  'sc_quality_nodes.py',51
  'sc_cert_memcleanup_split.py',69
  'sc_csw_fundrawtransaction.py',93
  'sc_proof_verifier_low_priority_threads.py',49
  'subsidyhalving.py',195
  'cbh_rpfix.py',45
  'cbh_rpcheck.py',26
  'tlsprotocols.py',12
  'mempool_double_spend.py',21
  'getblockmerkleroots.py',67
  'sc_block_partitions.py',60
  'sc_cert_bwt_amount_rounding.py',30
  'sc_csw_eviction_from_mempool.py',124
  'sc_csw_memcleanup_split.py',70
  'sc_csw_balance_exceeding.py',57
  'sc_stale_ft_and_mbtr.py',121
  'sc_cert_getblocktemplate.py',253
  'sc_cert_bt_immature_balances.py',40
  'sc_rpc_cmds_fee_handling.py',137
  'sc_cert_listsinceblock.py',70
  'sc_cert_dust.py',98
  'sc_keyrot.py',47
  'txindex.py',28
  'getblockexpanded.py',191
  'sc_rpc_cmds_json_output.py',68
  'sc_version.py',104
  'sc_getscgenesisinfo.py',86
  'fundaddresses.py',12
  'sc_getcertmaturityinfo.py',68
  'sc_big_commitment_tree.py',63
  'sc_big_commitment_tree_getblockmerkleroot.py',11
);
testScriptsExt=(
  'getblocktemplate_longpoll.py',120
  'getblocktemplate_proposals.py',57
  'getblocktemplate_blockmaxcomplexity.py',55
  'getblocktemplate_priority.py',39
  # 'pruning.py'                # disabled for Zen. Failed because of the issue #1302 in zcash
  'forknotify.py',27
  # 'hardforkdetection.py'      # disabled for Zen. Failed because of the issue #1302 in zcash
  # 'invalidateblock.py'        # disabled for Zen. Failed because of the issue #1302 in zcash
  'keypool.py',12
  'receivedby.py',30
  'rpcbind_test.py',60
  #  'script_test.py'
  'smartfees.py',158
  'maxblocksinflight.py',14
  'invalidblockrequest.py',40
  'invalidblockposthalving.py',113
  'p2p-acceptblock.py',202
  'replay_protection.py',22
  'headers_01.py',14
  'headers_02.py',22
  'headers_03.py',22
  'headers_04.py',26
  'headers_05.py',48
  'headers_06.py',44
  'headers_07.py',107
  'headers_08.py',25
  'headers_09.py',44
  'headers_10.py',36
  'checkblockatheight.py',103
  'sc_big_block.py',92
);

if [ "x$ENABLE_ZMQ" = "x1" ]; then
  testScripts+=('zmq_test.py',25)
fi

if [ "x$ENABLE_PROTON" = "x1" ]; then
  testScripts+=('proton_test.py',100)
fi

if [ "x$ENABLE_ADDRESS_INDEX" = "x1" ]; then
  testScripts+=('addressindex.py',34
                'spentindex.py',18
                'timestampindex.py',21
                'sc_cert_addressindex.py',128
                'sc_cert_addrmempool.py',46)
fi

# include extended tests
if [ ! -z "$EXTENDED" ] && [ "${EXTENDED}" = "true" ]; then
  testScripts+=( "${testScriptsExt[@]}" )
fi

# remove tests provided by --exclude= from testScripts
if [ ! -z "$EXCLUDE" ]; then
  for target in ${EXCLUDE//,/ }; do
    for i in "${!testScripts[@]}"; do
      if [ "${testScripts[i]}" = "$target" ] || [ "${testScripts[i]}" = "$target.py" ]; then
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
testList=$(${BUILDDIR}/qa/pull-tester/loadbalancer.py "${chunks}" "${chunk}" "${testScripts[@]}")

# convert back the load balancer output into an array. Spaces in filename are preserved
originalIFS="$IFS"; IFS='|'
read -a testScripts <<< ${testList}
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

  echo
}

if [ "x${ENABLE_BITCOIND}${ENABLE_UTILS}${ENABLE_WALLET}" = "x111" ]; then
  for (( i = 0; i < ${#testScripts[@]}; i++ )); do
    checkFileExists "${testScripts[$i]}"

    if [ -z "$1" ] || [ "${1:0:1}" = "-" ] || [ "$1" = "${testScripts[$i]}" ] || [ "$1.py" = "${testScripts[$i]}" ]; then
      runTestScript \
        "${testScripts[$i]}" \
        "${BUILDDIR}/qa/rpc-tests/${testScripts[$i]}" \
        --srcdir "${BUILDDIR}/src" ${passOn}
    fi
  done

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
