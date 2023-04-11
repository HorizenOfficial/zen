#!/usr/bin/env python3
#
# Execute all of the automated tests related to Zcash.
#

import argparse
import os
import re
import subprocess
import sys

REPOROOT = os.path.dirname(
    os.path.dirname(
        os.path.dirname(
            os.path.abspath(__file__)
        )
    )
)

def repofile(filename):
    return os.path.join(REPOROOT, filename)


#
# Custom test runners
#

RE_RPATH_RUNPATH = re.compile('No RPATH.*No RUNPATH')
RE_FORTIFY_AVAILABLE = re.compile('FORTIFY_SOURCE support available.*Yes')
RE_FORTIFY_USED = re.compile('Binary compiled with FORTIFY_SOURCE support.*Yes')

def test_rpath_runpath(filename):
    output = subprocess.check_output(
        [repofile('qa/zen/checksec.sh'), '--file', repofile(filename)]
    )
    if RE_RPATH_RUNPATH.search(output.decode('utf-8')):
        print('PASS: %s has no RPATH or RUNPATH.' % filename)
        return True
    else:
        print('FAIL: %s has an RPATH or a RUNPATH.' % filename)
        print(output)
        return False

def test_fortify_source(filename):
    proc = subprocess.Popen(
        [repofile('qa/zen/checksec.sh'), '--fortify-file', repofile(filename)],
        stdout=subprocess.PIPE,
    )
    line1 = proc.stdout.readline()
    line2 = proc.stdout.readline()
    proc.terminate()
    if RE_FORTIFY_AVAILABLE.search(line1.decode("utf-8")) and RE_FORTIFY_USED.search(line2.decode("utf-8")):
        print('PASS: %s has FORTIFY_SOURCE.' % filename)
        return True
    else:
        print('FAIL: %s is missing FORTIFY_SOURCE.' % filename)
        return False

def check_security_hardening():
    ret = True

    # PIE, RELRO, Canary, and NX are tested by make check-security.
    ret &= subprocess.call(['make', '-C', repofile('src'), 'check-security']) == 0
    ret &= test_rpath_runpath('src/zend')
    ret &= test_rpath_runpath('src/zen-cli')
    ret &= test_rpath_runpath('src/zen-gtest')
    ret &= test_rpath_runpath('src/zen-tx')
    ret &= test_rpath_runpath('src/test/test_bitcoin')
    ret &= test_rpath_runpath('src/zcash/GenerateParams')

    # NOTE: checksec.sh does not reliably determine whether FORTIFY_SOURCE
    # is enabled for the entire binary. See issue #915.
    ret &= test_fortify_source('src/zend')
    ret &= test_fortify_source('src/zen-cli')
    ret &= test_fortify_source('src/zen-gtest')
    ret &= test_fortify_source('src/zen-tx')
    ret &= test_fortify_source('src/test/test_bitcoin')
    ret &= test_fortify_source('src/zcash/GenerateParams')
    return ret

def ensure_no_dot_so_in_depends():
    try:
        os.environ['HOST']
    except NameError:
        host = 'x86_64-unknown-linux-gnu'
    else:
        host = os.environ['HOST']

    arch_dir = os.path.join(
        REPOROOT,
        'depends',
        host,
    )

    exit_code = 0

    if os.path.isdir(arch_dir):
        lib_dir = os.path.join(arch_dir, 'lib')
        libraries = os.listdir(lib_dir)

        for lib in libraries:
            if lib.find(".so") != -1:
                print(lib)
                exit_code = 1
    else:
        exit_code = 2
        print("arch-specific build dir not present: {}".format(arch_dir))
        print("Did you build the ./depends tree?")
        print("Are you on a currently unsupported architecture?")

    if exit_code == 0:
        print("PASS.")
    else:
        print("FAIL.")

    return exit_code == 0

def util_test():
    python = []
    if os.path.isfile('/usr/local/bin/python3'):
        python = ['/usr/local/bin/python3']

    return subprocess.call(
        python + [repofile('src/test/bitcoin-util-test.py')],
        cwd=repofile('src'),
        env={'PYTHONPATH': repofile('src/test'), 'srcdir': repofile('src')}
    ) == 0


#
# Tests
#

STAGES = [
    'btest',
    'gtest',
    'b-gtest_with_coverage',
    'sec-hard',
    'no-dot-so',
    'util-test',
    'secp256k1',
    'libsnark',
    'univalue',
    'rpc',
    'clang-tidy',
]

STAGE_COMMANDS = {
    'btest': [repofile('src/test/test_bitcoin'), '-p'],
    'gtest': [repofile('src/zen-gtest')],
    'b-gtest_with_coverage': ['make','cov_ci'],
    'sec-hard': check_security_hardening,
    'no-dot-so': ensure_no_dot_so_in_depends,
    'util-test': util_test,
    'secp256k1': ['make', '-C', repofile('src/secp256k1'), 'check'],
    'libsnark': ['make', '-C', repofile('src'), 'libsnark-tests'],
    'univalue': ['make', '-C', repofile('src/univalue'), 'check'],
    'rpc': [repofile('qa/pull-tester/rpc-tests.sh')],
    'clang-tidy': [repofile('contrib/ci-horizen/scripts/test/clang-tidy-launcher.sh')],
}


#
# Test driver
#

def run_stage(stage, options=None):
    print('Running stage %s' % stage)
    print('=' * (len(stage) + 14))
    print

    cmd = STAGE_COMMANDS[stage]
    if type(cmd) == type([]):
        if options:
            for option in options:
                cmd.append(option)
        ret = subprocess.call(cmd) == 0
    else:
        ret = cmd()

    print
    print('-' * (len(stage) + 15))
    print('Finished stage %s' % stage)
    print

    return ret

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--list-stages', dest='list', action='store_true')
    parser.add_argument('stage', nargs='*', default=STAGES,
                        help='One of %s'%STAGES)
    parser.add_argument('--rpc-extended', dest='extended',
                        action='store_true', help='run extended rpc tests')
    parser.add_argument('--rpc-exclude', dest='exclude',
                        action='store', help='comma separated string of rpc tests to exclude, see qa/rpc-tests/README.md for more')
    parser.add_argument('--rpc-split', dest='split',
                        action='store', help='string in format m:n, see qa/rpc-tests/README.md for more')
    parser.add_argument('--rpc-macrebalance', dest='macrebalance',
                        action='store_true', help='finetune the workload rebalancer for MacOS builds')
    parser.add_argument('--rpc-runonly', dest='runonly',
                        action='store', help='execute only a specific python test, see qa/rpc-tests/README.md for more')                        
    parser.add_argument('--coverage', dest='enable_cov',
                        action='store_true', help='Enables code coverage data collection')
    args = parser.parse_args()

    # Check for list
    if args.list:
        for s in STAGES:
            print(s)
        sys.exit(0)

    # Check validity of stages
    for s in args.stage:
        if s not in STAGES:
            print("Invalid stage '%s' (choose from %s)" % (s, STAGES))
            sys.exit(1)

    # Run the stages
    passed = True
    b_or_g_tests_with_covereage_done = False
    for s in args.stage:
        # Check for rpc test args
        if s == 'rpc':
            options=[]
            if args.extended:
                options.append('-extended')
            if args.exclude:
                options.append('-exclude=' + args.exclude)
            if args.split:
                options.append('-split=' + args.split)
            if args.macrebalance:
                options.append('-macrebalance')
            if args.runonly:
                options.append(args.runonly)
            if args.enable_cov:
                options.append('-coverage')
            passed &= run_stage(s, options)
        # When running tests with coverage enabled, normal calls to btest and gtest are superseded
        # by those defined into the Makefile, as they also include report filtering and submission
        # to Codacy
        elif args.enable_cov and (s == 'btest' or s == 'gtest'):
            if not b_or_g_tests_with_covereage_done:
                passed &= run_stage('b-gtest_with_coverage')
                b_or_g_tests_with_covereage_done = True # "make cov_ci" runs both btest and gtest!
        else:
            passed &= run_stage(s)

    if not passed:
        print("!!! One or more test stages failed !!!")
        sys.exit(1)

if __name__ == '__main__':
    main()
