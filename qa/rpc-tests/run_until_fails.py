# This is a utility Python script to spot sporadic failures in regression tests (and eventually debug them).
# It continously runs a Python test until it fails for some reason, printing the output to console.
# Please, note that only the output of the failing test is printed, the output of the succeeding tests is filtered out.
#
# Usage:
# python run_until_fails.py <test_file> [test options]
#
# Example usage:
# python run_until_fails.py addressindex.py
# python run_until_fails.py reindex.py --nocleanup

import subprocess
import sys

from datetime import datetime
from os import path

if len(sys.argv) < 2:
    print("Usage: run_until_fails.py test_name.py [test options]")
    sys.exit(1)

test_file = sys.argv[1]
test_command = sys.argv[1:]

if not path.isfile(test_file):
    print(f"Test file does not exist: {test_file}")
    sys.exit(1)

error_occurred = False

counter = 0

while not error_occurred:
    counter = counter + 1
    print(f"[{counter}] Running test: {test_file} - {datetime.now().strftime('%H:%M:%S')}")
    command_call = subprocess.Popen(["python3"]+test_command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
    output, errors = command_call.communicate()
    error_occurred = command_call.returncode != 0

    if error_occurred:
        print("Error occurred: " + str(command_call.returncode))
        print(output)
        sys.exit(1)
