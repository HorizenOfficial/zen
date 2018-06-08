#!/bin/bash
#
# Execute all of the automated tests related to Zen.
#

set -eu

SUITE_EXIT_STATUS=0
REPOROOT="$(readlink -f "$(dirname "$0")"/../../)"

function run_test_phase
{
    echo "===== BEGIN: $*"
    set +e
    eval "$@"
    if [ $? -eq 0 ]
    then
        echo "===== PASSED: $*"
    else
        echo "===== FAILED: $*"
        SUITE_EXIT_STATUS=1
    fi
    set -e
}

cd "${REPOROOT}"

# Test phases:
# ZEN_MOD_START
run_test_phase "${REPOROOT}/qa/zen/check-security-hardening.sh"
run_test_phase "${REPOROOT}/qa/zen/ensure-no-dot-so-in-depends.py"
# ZEN_MOD_END

# If make check fails, show test-suite.log as part of our run_test_phase
# output (and fail the phase with false):
run_test_phase make check '||' \
               '{' \
               echo '=== ./src/test-suite.log ===' ';' \
               cat './src/test-suite.log' ';' \
               false ';' \
               '}'

exit $SUITE_EXIT_STATUS






