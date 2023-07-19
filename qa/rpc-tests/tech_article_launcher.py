import subprocess
import sys

from datetime import datetime
from os import path

if len(sys.argv) != 2:
    sys.exit("UNSUPPORTED (launcher)")

if (sys.argv[1].startswith("algorithm=")):
    algorithm = sys.argv[1].replace("algorithm=", "")
else:
    sys.exit("UNSUPPORTED (launcher)")

if (algorithm not in  ["algo-new", "algo-old"]):
    sys.exit("UNSUPPORTED (launcher)")

if not path.isfile("tech_article.py"):
    sys.exit(f"File does not exist: tech_article.py")

for test_mode in ["few-low", "many-low", "many-lowmiddlehigh"]:
    for subtract_fee_from_amount in [False, True]:
        for indexer_offset in range(0, 10001, 1000):
            print(f"Starting with test_mode={test_mode}, algorithm={algorithm}, indexer_offset={indexer_offset}, subtract_fee_from_amount={subtract_fee_from_amount}")
            command_call = subprocess.Popen(["python3", "tech_article.py", f"test_mode={test_mode}", f"algorithm={algorithm}", f"indexer_offset={indexer_offset}", f"subtract_fee_from_amount={subtract_fee_from_amount}"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
            output, errors = command_call.communicate()
            error_occurred = command_call.returncode != 0
            print(f"Finishing with test_mode={test_mode}, algorithm={algorithm}, indexer_offset={indexer_offset}, subtract_fee_from_amount={subtract_fee_from_amount}")
