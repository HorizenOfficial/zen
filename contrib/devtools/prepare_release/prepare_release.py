#!/usr/bin/python3

import datetime
import json
import os
import re
import subprocess
import urllib.request
import yaml
import sys
import shutil

# dictionary keys
k_repository_root = "repository_root"
k_version = "version"
k_build = "build"
k_mainnet = "mainnet"
k_testnet = "testnet"
k_checkpoint_height = "checkpoint_height"
k_total_transactions = "total_transactions"
k_release_date = "release_date"
k_approx_release_height = "approx_release_height"
k_weeks_until_deprecation = "weeks_until_deprecation"
k_previous_version = "previous_version"
k_release_notes_file = "release_notes_file"
k_script_steps = "script_steps"
k_set_client_version = "set_client_version"
k_update_checkpoints = "update_checkpoints"
k_update_changelog = "update_changelog"
k_update_deprecation_height = "update_deprecation_height"
k_build_zend = "build_zend"
k_update_man_pages = "update_man_pages"
k_update_release_notes = "update_release_notes"
k_update_readme = "update_readme"
k_skip = "skip"
k_stop = "stop"


# git functions
def git_check_currently_on_main():
    result = subprocess.run(["git", "rev-parse", "--abbrev-ref", "HEAD"], capture_output=True, text=True, cwd=config[k_repository_root])
    return result.returncode == 0 and result.stdout.rstrip() == "main"

def git_check_pending_changes():
    result = subprocess.run(["git", "diff"], capture_output=True, text=True, cwd=config[k_repository_root])
    return result.returncode == 0 and result.stdout != ""

def git_commit(commit_title: str):
   subprocess.run(["git", "add", "."], cwd=config[k_repository_root])  # Add all changes to the index
   result = subprocess.run(["git", "commit", "-S", "-m", commit_title], capture_output=True, text=True, cwd=config[k_repository_root])  # Commit changes with the given message
   if (result.returncode != 0 or git_check_pending_changes()):
        print("Commit failed")
        sys.exit()

def git_create_branch(branch_name: str):
   result = subprocess.run(["git", "rev-parse", "--verify", branch_name], capture_output=True, text=True, cwd=config[k_repository_root])  # Commit changes with the given message
   if (result.returncode == 0):
        print("Branch already exist; check if script has been already launched or if provided new version is wrong")
        sys.exit()
   result = subprocess.run(["git", "checkout", "-b", branch_name], capture_output=True, text=True, cwd=config[k_repository_root])  # Commit changes with the given message
   if (result.returncode != 0):
        print("Branch creation failed")
        sys.exit()

# utility function
def get_version_string_details(version_string: str):
    assert(version_string.count(".") == 2)
    digits = [int(digit) for digit in version_string if digit.isdigit()]
    assert(len(digits) == 3)
    return digits

def replace_string_in_file(filepath: str, old_string_regex: str, new_string: str):
    with open(filepath, "r") as file:
        file_data = file.read()

    new_data = re.sub(old_string_regex, new_string, file_data)

    with open(filepath, "w") as file:
        file.write(new_data)

def get_last_blocks(explorer_url: str):
    # Retrieve the JSON data from the "blocks" endpoint
    request = urllib.request.Request(f"{explorer_url}api/blocks", headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(request) as response:
        json_data = response.read()

    # Parse and return the JSON data
    return json.loads(json_data)

def get_block_hash_from_height(explorer_url: str, block_height: int):
    # Retrieve the JSON data from the "blocks" endpoint
    request = urllib.request.Request(f"{explorer_url}api/block-index/{block_height}", headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(request) as response:
        json_data = response.read()

    # Parse and return the JSON data
    return json.loads(json_data)["blockHash"]

def get_block(explorer_url: str, block_hash: str):
    # Retrieve the JSON data from the "blocks" endpoint
    request = urllib.request.Request(f"{explorer_url}api/block/{block_hash}", headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(request) as response:
        json_data = response.read()

    # Parse and return the JSON data
    return json.loads(json_data)

def get_block_from_height(explorer_url: str, block_height: int):
    return get_block(explorer_url, get_block_hash_from_height(explorer_url, block_height))

def get_checkpoint_candidate(net: str, explorer_url: str):
    if (interactive):
        config[net] = {}
        config[net][k_checkpoint_height] = input(f"Enter checkpoint height for {net} (no input for using last block): ")

    if (config[net][k_checkpoint_height] != ""):
        checkpoint_candidate_height = int(config[net][k_checkpoint_height])
    else:
        last_blocks = get_last_blocks(explorer_url)
        checkpoint_candidate_height = last_blocks["blocks"][0]["height"]
        
    # some rounding
    checkpoint_candidate_height = checkpoint_candidate_height // 100 * 100

    # Get the checkpoint block
    checkpoint_block = get_block_from_height(explorer_url, checkpoint_candidate_height)

    block_timestamp = checkpoint_block["time"]
    parent_block_hash = checkpoint_block["previousblockhash"]

    for _ in range(3):
        parent_block = get_block(explorer_url, parent_block_hash)
        current_timestamp = parent_block["time"]
        assert(current_timestamp < block_timestamp)
        block_timestamp = current_timestamp
        parent_block_hash = parent_block["previousblockhash"]

    return checkpoint_block

def find_lines_containing(file_path: str, string_to_find: str):
    line_numbers = []

    with open(file_path, "r") as file:
        # Read all lines of the file into a list
        lines = file.readlines()

        # Find the lines containing the string to find
        line_numbers = [i for i, line in enumerate(lines) if string_to_find in line]

    return line_numbers

def read_file_line(file_path: str, line: int):
    with open(file_path, "r") as file:
        # Read all lines of the file into a list
        lines = file.readlines()

        return lines[line]

def replace_file_line(file_path: str, line_number: int, line_content: str):
    # read in the file
    with open(file_path, "r") as file:
        lines = file.readlines()

    # replace the line at the specified index
    lines[line_number] = line_content + "\n"

    # write the updated lines back to the file
    with open(file_path, "w") as file:
        file.writelines(lines)

def insert_line_into_file(file_path: str, line_number: int, line_content: str):
    # read in the file
    with open(file_path, "r") as file:
        lines = file.readlines()

    # replace the line at the specified index
    lines.insert(line_number + 1, line_content)

    # write the updated lines back to the file
    with open(file_path, "w") as file:
        file.writelines(lines)

def ask_for_step_skip(script_step: str):
    config[k_script_steps][script_step] = {}
    config[k_script_steps][script_step][k_stop] = False
    if (input("Do you want to skip this step? (Y/N)").upper() == "Y"):
        config[k_script_steps][script_step][k_skip] = True
    else:
        config[k_script_steps][script_step][k_skip] = False


# script steps functions
def initialize():
    global config
    global interactive
    
    config_file = ""
    if (len(sys.argv) > 1):
        config_file = sys.argv[1]
    else:
        # config_file = "config.yaml"
        config_file = input("Enter config file path (no input for interactive session): ")
    if (os.path.exists(config_file)):
        with open(config_file, "r") as stream:
            try:
                config = yaml.safe_load(stream)
                interactive = False
            except yaml.YAMLError as exc:
                print(exc)
    else:
        print("Config file not available, proceeding with interactive session...")

    if (interactive):
        config[k_repository_root] = input("Enter the repository root path: ")
    
    if (not git_check_currently_on_main()):
        print("Currently selected branch is not \"main\"; checkout \"main\" branch and retry.")
        sys.exit()

    if (git_check_pending_changes()):
        print("There are pending changes in selected repository; commit or stash them and retry.")
        sys.exit()

    if (interactive):
        config[k_version] = input("Enter the new version string (e.g. 3.3.1): ")
        print("Enter the build number:")
        print("Please, use the following values:")
        print("[ 1, 24] when releasing a beta version (e.g. 3.3.1-beta1, 3.3.1-beta2, etc.)")
        print("[25, 49] when releasing a release candidate (e.g. 3.3.1-rc1, 3.3.1-rc2, etc.)")
        print("    [50] when making a standard release (e.g. 3.3.1)")
        config[k_build] = input("")

    branch_name = f"release/{config[k_version]}"
    build_number = int(config[k_build])
    if (1 <= build_number and build_number <= 24):
        branch_name += f"-beta{config[k_build]}"
    elif (25 <= build_number and build_number <= 49):
        branch_name += f"-rc{config[k_build]}"
    elif (50 == build_number):
        branch_name += ""
    else:
        print("Wrong build number; modify and retry.")
        sys.exit()
    
    git_create_branch(branch_name)

    return config

def set_client_version():
    version_digits = get_version_string_details(config[k_version])

    replace_string_in_file(os.path.join(config[k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_MAJOR, (\d+)\)",    f"define(_CLIENT_VERSION_MAJOR, {version_digits[0]})")
    replace_string_in_file(os.path.join(config[k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_MINOR, (\d+)\)",    f"define(_CLIENT_VERSION_MINOR, {version_digits[1]})")
    replace_string_in_file(os.path.join(config[k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_REVISION, (\d+)\)", f"define(_CLIENT_VERSION_REVISION, {version_digits[2]})")
    replace_string_in_file(os.path.join(config[k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_BUILD, (\d+)\)",    f"define(_CLIENT_VERSION_BUILD, {config[k_build]})")
    replace_string_in_file(os.path.join(config[k_repository_root], "configure.ac"), r"define\(_COPYRIGHT_YEAR, (\d+)\)",          f"define(_COPYRIGHT_YEAR, {datetime.date.today().year})")

    replace_string_in_file(os.path.join(config[k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_MAJOR\s+)(\d+)",    f"\\g<1>{version_digits[0]}")
    replace_string_in_file(os.path.join(config[k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_MINOR\s+)(\d+)",    f"\\g<1>{version_digits[1]}")
    replace_string_in_file(os.path.join(config[k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_REVISION\s+)(\d+)", f"\\g<1>{version_digits[2]}")
    replace_string_in_file(os.path.join(config[k_repository_root], "src/clientversion.h"), r"(#define\s+COPYRIGHT_YEAR\s+)(\d+)",          f"\\g<1>{datetime.date.today().year}")

    git_commit(f"Set clientversion {config[k_version]} (build {config[k_build]})")

def update_checkpoints():
    # Get a mainnet checkpoint candidate
    mainnet_checkpoint = get_checkpoint_candidate(k_mainnet, "https://explorer.horizen.io/")
    testnet_checkpoint = get_checkpoint_candidate(k_testnet, "https://explorer-testnet.horizen.io/")

    # Find the line numbers containing the last checkpoint
    # for both mainnet and testnet
    line_numbers = find_lines_containing(os.path.join(config[k_repository_root], "src/chainparams.cpp"), r'")),')
    print(line_numbers)

    for line_number in line_numbers[:2]:
        line = read_file_line(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_number)
        parts = line.rstrip().rsplit(",", 1)
        line = "".join(parts)
        replace_file_line(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_number, line)

    new_line = f"            ( {mainnet_checkpoint['height']}, uint256S(\"0x{mainnet_checkpoint['hash']}\")),\n"
    insert_line_into_file(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_numbers[0], new_line)

    # After the insertion, the line number are updated
    line_numbers[1] = line_numbers[1] + 1

    new_line = f"            ( {testnet_checkpoint['height']}, uint256S(\"0x{testnet_checkpoint['hash']}\")),\n"
    insert_line_into_file(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_numbers[1], new_line)

    line_numbers = find_lines_containing(os.path.join(config[k_repository_root], "src/chainparams.cpp"), "UNIX timestamp of last checkpoint block")

    line = read_file_line(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_numbers[0])
    line = re.sub(r"(\d+)", str(mainnet_checkpoint["time"]), line).rstrip()
    print(f"Substituted string {repr(line)}")
    replace_file_line(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_numbers[0], line)

    line = read_file_line(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_numbers[1])
    line = re.sub(r"(\d+)", str(testnet_checkpoint["time"]), line).rstrip()
    print(f"Substituted string {repr(line)}")
    replace_file_line(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_numbers[1], line)

    if (interactive):
        config[k_mainnet][k_total_transactions] = input(f"Please, enter the total number of transactions for MAINNET block {mainnet_checkpoint['height']}: ")
    line = read_file_line(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_numbers[0] + 1)
    line = re.sub(r"(\d+)", config[k_mainnet][k_total_transactions], line).rstrip()
    replace_file_line(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_numbers[0] + 1, line)
    
    if (interactive):
        config[k_testnet][k_total_transactions] = input(f"Please, enter the total number of transactions for TESTNET block {testnet_checkpoint['height']}: ")
    line = read_file_line(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_numbers[1] + 1)
    line = re.sub(r"(\d+)", config[k_testnet][k_total_transactions], line).rstrip()
    replace_file_line(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_numbers[1] + 1, line)

    average_mainnet_transactions = round(int(config[k_testnet][k_total_transactions]) / (mainnet_checkpoint['height'] / (24 * 24)))
    line = read_file_line(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_numbers[0] + 3)
    line = re.sub(r"(\d+)", str(average_mainnet_transactions), line).rstrip()
    replace_file_line(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_numbers[0] + 3, line)

    average_testnet_transactions = round(int(config[k_testnet][k_total_transactions]) / (testnet_checkpoint['height'] / (24 * 24)))
    line = read_file_line(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_numbers[1] + 3)
    line = re.sub(r"(\d+)", str(average_testnet_transactions), line).rstrip()
    replace_file_line(os.path.join(config[k_repository_root], "src/chainparams.cpp"), line_numbers[1] + 3, line)

    git_commit("Update checkpoint blocks")

def update_changelog():
    if (interactive):
        config[k_release_date] = input("Please, enter the new release date (e.g. Mon, 02 Jan 2023): ")
    
    line = read_file_line(os.path.join(config[k_repository_root], "contrib/debian/changelog"), 0)
    current_version = line[line.find("(")+1:line.find(")")]
    line = line.replace(current_version, config[k_version]).rstrip()
    replace_file_line(os.path.join(config[k_repository_root], "contrib/debian/changelog"), 0, line)

    line = read_file_line(os.path.join(config[k_repository_root], "contrib/debian/changelog"), 4)
    print(line)
    line = re.sub(r"\w{3}, \d{1,2} \w{3} \d{4}", config[k_release_date], line).rstrip()
    print(line)
    replace_file_line(os.path.join(config[k_repository_root], "contrib/debian/changelog"), 4, line)

    git_commit("Update Debian package info")

def update_deprecation_height():
    if (interactive):
        config[k_approx_release_height] = input("Enter the approximate release height (mainnet): ")
        config[k_weeks_until_deprecation] = input("Enter the weeks until deprecation: ")

    approx_release_height_line = read_file_line(os.path.join(config[k_repository_root], "src/deprecation.h"), 7)
    weeks_until_deprecation_line = read_file_line(os.path.join(config[k_repository_root], "src/deprecation.h"), 8)

    approx_release_height_line = re.sub(r"(\d+)", str(config[k_approx_release_height]), approx_release_height_line).rstrip()
    replace_file_line(os.path.join(config[k_repository_root], "src/deprecation.h"), 7, approx_release_height_line)

    weeks_until_deprecation_line = re.sub(r"(\d+)", str(config[k_weeks_until_deprecation]), weeks_until_deprecation_line).rstrip()
    replace_file_line(os.path.join(config[k_repository_root], "src/deprecation.h"), 8, weeks_until_deprecation_line)

    blocks_per_hour = 24
    blocks_per_day = 24 * blocks_per_hour
    blocks_per_week = 7 * blocks_per_day
    future_deprecation_height = int(config[k_approx_release_height]) + blocks_per_week * int(config[k_weeks_until_deprecation])

    # Get last block time
    last_block = get_last_blocks("https://explorer.horizen.io/")["blocks"][0]
    last_block_height = int(last_block["height"])
    last_block_timestamp = last_block["time"]
    last_block_date = datetime.datetime.fromtimestamp(last_block_timestamp)

    # Estimate the block time over the last 100'000 blocks
    reference_block = get_block_from_height("https://explorer.horizen.io/", last_block_height - 100000)
    reference_block_timestamp = reference_block["time"]
    block_time = (last_block_timestamp - reference_block_timestamp) / 100000

    # Estimate the datetime of the future_deprecation_height
    future_deprecation_date = last_block_date + datetime.timedelta(seconds=(future_deprecation_height - last_block_height) * block_time)

    # Estimate the next deprecation date
    date_string = future_deprecation_date.strftime('%Y-%m-%d')

    git_commit(f"Set deprecation height {future_deprecation_height}/{date_string}")

def build_zend():
    result_clean = subprocess.run(["make", "distclean"], cwd=config[k_repository_root])
    result_build = subprocess.run([f"./zcutil/build.sh", "-j8"], cwd=config[k_repository_root])
    if (result_build.returncode != 0):
        print("Failure building zend")
        sys.exit()

def update_man_pages():
    subprocess.run(["./contrib/devtools/gen-manpages.sh"], cwd=config[k_repository_root])
    git_commit("Update man pages")

def update_release_notes():
    release_notes_file_path = f"./doc/release-notes/release-notes-{config[k_version]}.md"

    if (interactive):
        config[k_previous_version] = input(f"Please, enter the last published version (before {config[k_version]}): ")

    if not config[k_previous_version].startswith("v"):
        config[k_previous_version] = "v" + config[k_previous_version]

    # Automatically update AUTHORS.md and release notes
    subprocess.run(["python3", "./zcutil/release-notes.py", "--version", config[k_version], "--prev", config[k_previous_version]], cwd=config[k_repository_root])

    # Remove automatically generated release notes
    subprocess.run(["rm", release_notes_file_path], cwd=config[k_repository_root])

    if (interactive):
        for i in range(2): # just to allow a re-prompt of the instruction message
            input(f"Please, add release notes to {release_notes_file_path} and then press enter to continue...")
            if (os.path.exists(os.path.join(config[k_repository_root], release_notes_file_path))):
                break
    else:
        if (os.path.exists(config[k_release_notes_file])):
            shutil.copyfile(config[k_release_notes_file], os.path.join(config[k_repository_root], release_notes_file_path))
        else:
            print(f"Release notes file {config[k_release_notes_file]} does not exist; please, check and retry.")
            sys.exit()

    git_commit("Add release notes")

def update_readme():
    replace_file_line(os.path.join(config[k_repository_root], "README.md"), 0, f"Zend {config[k_version]}".rstrip())

    git_commit("Update README")


config = {}
interactive = True

print("\n********** Step 0: setup config **********\n")
initialize()

print("\n********** Step 1: set the new client version **********\n")
if (interactive):
    config[k_script_steps] = {}
    ask_for_step_skip(k_set_client_version)
if (bool(config[k_script_steps][k_set_client_version][k_stop])):
    input("Press a key to proceed")
if (not bool(config[k_script_steps][k_set_client_version][k_skip])):
    set_client_version()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 2: update the mainnet and testnet checkpoints **********\n")
if (interactive):
    ask_for_step_skip(k_update_checkpoints)
if (bool(config[k_script_steps][k_update_checkpoints][k_stop])):
    input("Press a key to proceed")
if (not bool(config[k_script_steps][k_update_checkpoints][k_skip])):
    update_checkpoints()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 3: update changelog **********\n")
if (interactive):
    ask_for_step_skip(k_update_changelog)
if (bool(config[k_script_steps][k_update_changelog][k_stop])):
    input("Press a key to proceed")
if (not bool(config[k_script_steps][k_update_changelog][k_skip])):
    update_changelog()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 4: update deprecation height **********\n")
if (interactive):
    ask_for_step_skip(k_update_deprecation_height)
if (bool(config[k_script_steps][k_update_deprecation_height][k_stop])):
    input("Press a key to proceed")
if (not bool(config[k_script_steps][k_update_deprecation_height][k_skip])):
    update_deprecation_height()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 5: build Zend **********\n")
if (interactive):
    ask_for_step_skip(k_build_zend)
if (bool(config[k_script_steps][k_build_zend][k_stop])):
    input("Press a key to proceed")
if (not bool(config[k_script_steps][k_build_zend][k_skip])):
    build_zend()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 6: update man pages **********\n")
if (interactive):
    ask_for_step_skip(k_update_man_pages)
if (bool(config[k_script_steps][k_update_man_pages][k_stop])):
    input("Press a key to proceed")
if (not bool(config[k_script_steps][k_update_man_pages][k_skip])):
    update_man_pages()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 7: update release_notes **********\n")
if (interactive):
    ask_for_step_skip(k_update_release_notes)
if (bool(config[k_script_steps][k_update_release_notes][k_stop])):
    input("Press a key to proceed")
if (not bool(config[k_script_steps][k_update_release_notes][k_skip])):
    update_release_notes()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 8: update readme **********\n")
if (interactive):
    ask_for_step_skip(k_update_readme)
if (bool(config[k_script_steps][k_update_readme][k_stop])):
    input("Press a key to proceed")
if (not bool(config[k_script_steps][k_update_readme][k_skip])):
    update_readme()
    print("Done")
else:
    print("Skipped")

# TODO:
# - Remove code duplication (in particular of string variables) [PARTIALLY DONE]
# - Organize code in modules [NOT DONE]
# - Improve error handling and exceptions [PARTIALLY DONE]
# - Retrieve the total transactions from the debug.log file [HOW TO?]