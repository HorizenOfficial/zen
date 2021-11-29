import os.path
import subprocess
import sys

from os import path

if len(sys.argv) != 2:
    print("Usage: run_until_fails.py test_name.py")
    sys.exit(1)

test_file = sys.argv[1]

if not path.isfile(test_file):
    print("Test file does not exist: " + test_file)
    sys.exit(1)

error_occurred = False

while not error_occurred:
    print("Running test: " + test_file)
    command_call = subprocess.Popen(["python", test_file], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output, errors = command_call.communicate()
    error_occurred = command_call.returncode != 0

    if error_occurred:
        print("Error occurred: " + str(command_call.returncode))
        print(output)
        sys.exit(1)