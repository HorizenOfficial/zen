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
    -specific=*)
      SPECIFIC="${i#*=}"
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

testScripts=(
  'paymentdisclosure.py'
  'prioritisetransaction.py'
  'wallet_treestate.py'
  'wallet_protectcoinbase.py'
  'wallet_shieldcoinbase.py'
  'wallet_mergetoaddress.py'
  'wallet_mergetoaddress_2.py'
  'wallet.py'
  'wallet_nullifiers.py'
  'wallet_1941.py'
  'wallet_grothtx.py'
  'listtransactions.py'
  'mempool_resurrect_test.py'
  'txn_doublespend.py'
  'txn_doublespend.py --mineblock'
  'getchaintips.py'
  'rawtransactions.py'
  'rest.py'
  'mempool_spendcoinbase.py'
  'mempool_coinbase_spends.py'
  'mempool_tx_input_limit.py'
  'httpbasics.py'
  'zapwallettxes.py'
  'proxy_test.py'
  'merkle_blocks.py'
  'fundrawtransaction.py'
  'signrawtransactions.py'
  'walletbackup.py'
  'key_import_export.py'
  'nodehandling.py'
  'reindex.py'
  'decodescript.py'
  'disablewallet.py'
  'zcjoinsplit.py'
  'zcjoinsplitdoublespend.py'
  'zkey_import_export.py'
  'getblocktemplate.py'
  'bip65-cltv-p2p.py'
  'bipdersig-p2p.py'
  'nulldata.py'
  'blockdelay.py'
  'blockdelay_2.py'
  'z_sendmany.py'
  'sc_create.py'
  'sc_create_2.py'
  'sc_split.py'
  'sc_invalidate.py'
  'sc_cert_base.py'
  'sc_cert_fee.py'
  'sc_cert_epoch.py'
  'sc_cert_invalidate.py'
  'sc_fwd_maturity.py'
  'sc_rawcertificate.py'
  'getunconfirmedtxdata.py'
  'sc_cr_and_fw_in_mempool.py'
  'sc_cert_change.py'
  'sc_cert_orphans.py'
  'sc_cert_maturity.py'
  'sbh_rpc_cmds.py'
  'sc_cert_ceasing.py'
  'sc_cert_customfields.py'
  'sc_cert_getraw.py'
  'sc_quality_mempool.py'
  'sc_ft_and_mbtr_fees.py'
  'sc_bwt_request.py'
  'sc_cert_quality_wallet.py'
  'ws_messages.py'
  'ws_getsidechainversions.py'
  'sc_cert_ceasing_split.py'
  'sc_async_proof_verifier.py'
  'sc_quality_blockchain.py'
  'sc_quality_voiding.py'
  'sc_csw_actcertdata.py'
  'sc_csw_actcertdata_null.py'
  'sc_cert_ceasing_sg.py'
  'sc_csw_nullifier.py'
  'sc_getscinfo.py'
  'sc_quality_nodes.py'
  'sc_cert_memcleanup_split.py'
  'sc_csw_fundrawtransaction.py'
  'sc_proof_verifier_low_priority_threads.py'
  'subsidyhalving.py'
  'cbh_rpfix.py'
  'cbh_rpcheck.py'
  'tlsprotocols.py'
  'getblockmerkleroots.py'
  'sc_block_partitions.py'
  'sc_cert_bwt_amount_rounding.py'
  'sc_csw_eviction_from_mempool.py'
  'sc_csw_memcleanup_split.py'
  'sc_csw_balance_exceeding.py'
  'sc_stale_ft_and_mbtr.py'
  'sc_cert_getblocktemplate.py'
  'sc_cert_bt_immature_balances.py'
  'sc_rpc_cmds_fee_handling.py'
  'sc_cert_listsinceblock.py'
  'sc_cert_dust.py'
  'txindex.py'
  'getblockexpanded.py'
  'sc_rpc_cmds_json_output.py'
  'sc_getscgenesisinfo.py'
  'fundaddresses.py'
);
testScriptsExt=(
  'getblocktemplate_longpoll.py'
  'getblocktemplate_proposals.py'
  'getblocktemplate_blockmaxcomplexity.py'
  'getblocktemplate_priority.py'
# 'pruning.py'                # disabled for Zen. Failed because of the issue #1302 in zcash
  'forknotify.py'
# 'hardforkdetection.py'      # disabled for Zen. Failed because of the issue #1302 in zcash
# 'invalidateblock.py'        # disabled for Zen. Failed because of the issue #1302 in zcash
  'keypool.py'
  'receivedby.py'
  'rpcbind_test.py'
#  'script_test.py'
  'smartfees.py'
  'maxblocksinflight.py'
  'invalidblockrequest.py'
  'invalidblockposthalving.py'
  'p2p-acceptblock.py'
  'replay_protection.py'
  'headers_01.py'
  'headers_02.py'
  'headers_03.py'
  'headers_04.py'
  'headers_05.py'
  'headers_06.py'
  'headers_07.py'
  'headers_08.py'
  'headers_09.py'
  'headers_10.py'
  'checkblockatheight.py'
  'sc_big_block.py'
);

if [ "x$ENABLE_ZMQ" = "x1" ]; then
  testScripts+=('zmq_test.py')
fi

if [ "x$ENABLE_PROTON" = "x1" ]; then
  testScripts+=('proton_test.py')
fi

if [ "x$ENABLE_ADDRESS_INDEX" = "x1" ]; then
  testScripts+=('addressindex.py'
                'spentindex.py'
                'timestampindex.py'
                'sc_cert_addressindex.py'
                'sc_cert_addrmempool.py')
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

if [ ! -z "$SPECIFIC" ]; then
	testScripts=( "$SPECIFIC" )
fi

# split array into m parts and only run tests of part n where SPLIT=m:n
if [ ! -z "$SPLIT" ]; then
  chunks="${SPLIT%*:*}"
  chunk="${SPLIT#*:}"
  chunkSize="$((${#testScripts[@]}/${chunks}))"
  start=0
  for (( i = 1; i <= $chunks; i++ )); do
    if [ $i -eq $chunk ]; then
      break
    fi
    start=$(($start+$chunkSize))
  done
  if [ $chunks -eq $chunk ]; then
    testScripts=( "${testScripts[@]:$start}" )
  else
    testScripts=( "${testScripts[@]:$start:$chunkSize}" )
  fi
fi

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

  if eval "$@"; then
    successCount=$(expr $successCount + 1)
    echo "--- Success: ${testName} ---"
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
