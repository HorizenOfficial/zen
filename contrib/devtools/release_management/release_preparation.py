#!/usr/bin/python3

import datetime
import os
import re
import subprocess
import yaml
import sys
import shutil
import release_management_utils as rmu


def get_checkpoint_candidate(net: str, explorer_url: str):
    if (interactive):
        config[net] = {}
        config[net][rmu.k_checkpoint_height] = input(f"Enter checkpoint height for {net} (no input for using last block): ")

    if (config[net][rmu.k_checkpoint_height] != ""):
        checkpoint_candidate_height = int(config[net][rmu.k_checkpoint_height])
    else:
        last_blocks = rmu.get_last_blocks(explorer_url)
        checkpoint_candidate_height = last_blocks["blocks"][0]["height"]
        
    # some rounding
    checkpoint_candidate_height = checkpoint_candidate_height // 100 * 100

    # Get the checkpoint block
    checkpoint_block = rmu.get_block_from_height(explorer_url, checkpoint_candidate_height)

    block_timestamp = checkpoint_block["time"]
    parent_block_hash = checkpoint_block["previousblockhash"]

    for _ in range(3):
        parent_block = rmu.get_block(explorer_url, parent_block_hash)
        current_timestamp = parent_block["time"]
        assert(current_timestamp < block_timestamp)
        block_timestamp = current_timestamp
        parent_block_hash = parent_block["previousblockhash"]

    return checkpoint_block


# script steps functions
def initialize():
    global config
    global interactive
    
    config_file = ""
    if (len(sys.argv) > 1):
        config_file = sys.argv[1]
    else:
        config_file = input("Enter config file path (no input for interactive session): ")
    if (os.path.exists(config_file)):
        with open(config_file, "r") as stream:
            try:
                rmu.config = yaml.safe_load(stream)
                interactive = False
            except yaml.YAMLError as exc:
                print(exc)
    else:
        print("Config file not available, proceeding with interactive session...")
    config = rmu.config

    if (interactive):
        config[rmu.k_script_steps] = {}
        config[rmu.k_repository_root] = input("Enter the repository root path: ")
    
    if (interactive):
        rmu.ask_for_step_skip(rmu.k_initialize_check_main_checked_out, rmu.k_initialize_check_main_checked_out)
    if (not bool(config[rmu.k_script_steps][rmu.k_initialize_check_main_checked_out][rmu.k_skip])):
        if (bool(config[rmu.k_script_steps][rmu.k_initialize_check_main_checked_out][rmu.k_stop])):
            input("Press a key to proceed")
        if (not rmu.git_check_currently_main_checked_out(config[rmu.k_repository_root])):
            print("Currently selected branch is not \"main\"; checkout \"main\" branch and retry.")
            sys.exit(-1)
    else:
        print("Skipped")

    if (interactive):
        rmu.ask_for_step_skip(rmu.k_initialize_check_no_pending_changes, rmu.k_initialize_check_no_pending_changes)
    if (not bool(config[rmu.k_script_steps][rmu.k_initialize_check_no_pending_changes][rmu.k_skip])):
        if (bool(config[rmu.k_script_steps][rmu.k_initialize_check_no_pending_changes][rmu.k_stop])):
            input("Press a key to proceed")
        if (rmu.git_check_pending_changes(config[rmu.k_repository_root])):
            print("There are pending changes in selected repository; commit or stash them and retry.")
            sys.exit(-1)
    else:
        print("Skipped")

    if (interactive):
        config[rmu.k_version] = input("Enter the new version (format major.minor.patch, e.g. 3.3.1): ")
        print("Enter the build number:")
        print("Please, use the following values:")
        print("[ 0, 24] when releasing a beta version (e.g. 3.3.1-beta1, 3.3.1-beta2, etc.)")
        print("[25, 49] when releasing a release candidate (e.g. 3.3.1-rc1, 3.3.1-rc2, etc.)")
        print("    [50] when releasing an official release (e.g. 3.3.1)")
        print("   [51+] when releasing an alpha version (e.g. 3.3.1-alpha1, 3.3.1-alpha2, etc.)")
        config[rmu.k_build_number] = input("")

    build_number = int(config[rmu.k_build_number])
    if (0 > build_number):
        print("Wrong build number; modify and retry.")
        sys.exit(-1)

    if (interactive):
        rmu.ask_for_step_skip(rmu.k_initialize_create_branch, rmu.k_initialize_create_branch)
    if (not bool(config[rmu.k_script_steps][rmu.k_initialize_create_branch][rmu.k_skip])):
        if (bool(config[rmu.k_script_steps][rmu.k_initialize_create_branch][rmu.k_stop])):
            input("Press a key to proceed")
        prepare_release_branch_name = f"prepare_release/{config[rmu.k_version]}{rmu.get_build_suffix(build_number)}"
        if (rmu.git_check_branch_existence(config[rmu.k_repository_root], prepare_release_branch_name)):
            print("Branch already exists; check if script has been already launched or if provided new version is wrong")
            sys.exit(-1)
        if (not rmu.git_create_branch(config[rmu.k_repository_root], prepare_release_branch_name)):
            print("Branch creation failed")
            sys.exit(-1)
    else:
        print("Skipped")

    if ("contrib/devtools/release_management/configs" in config_file):
        if (not rmu.git_commit(config[rmu.k_repository_root], "Initialize release preparation branch", ["contrib/devtools/release_management/configs"])):
            print("Commit failed")
            sys.exit(-1)

def set_client_version():
    version_digits = rmu.get_version_string_details(config[rmu.k_version])

    rep_maj = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_MAJOR, (\d+)\)",    f"define(_CLIENT_VERSION_MAJOR, {version_digits[0]})")
    rep_min = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_MINOR, (\d+)\)",    f"define(_CLIENT_VERSION_MINOR, {version_digits[1]})")
    rep_rev = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_REVISION, (\d+)\)", f"define(_CLIENT_VERSION_REVISION, {version_digits[2]})")
    rep_bld = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_BUILD, (\d+)\)",    f"define(_CLIENT_VERSION_BUILD, {config[rmu.k_build_number]})")
    if (not rep_maj and not rep_min and not rep_rev and not rep_bld):
        print("Version replacement failed (configure.ac)")
        sys.exit(-1)
    rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_IS_RELEASE, .*\)", f'define(_CLIENT_VERSION_IS_RELEASE, {"true" if config[rmu.k_build_number] == "50" else "false"})')
    rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "configure.ac"), r"define\(_COPYRIGHT_YEAR, (\d+)\)", f"define(_COPYRIGHT_YEAR, {datetime.date.today().year})")

    rep_maj = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_MAJOR\s+)(\d+)",    f"\\g<1>{version_digits[0]}")
    rep_min = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_MINOR\s+)(\d+)",    f"\\g<1>{version_digits[1]}")
    rep_rev = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_REVISION\s+)(\d+)", f"\\g<1>{version_digits[2]}")
    rep_bld = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_BUILD\s+)(\d+)",    f"\\g<1>{config[rmu.k_build_number]}")
    if (not rep_maj and not rep_min and not rep_rev and not rep_bld):
        print("Version replacement failed (src/clientversion.h)")
        sys.exit(-1)
    rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_IS_RELEASE\s+)(\w+)", f'\\g<1>{"true" if config[rmu.k_build_number] == "50" else "false"}')
    rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "src/clientversion.h"), r"(#define\s+COPYRIGHT_YEAR\s+)(\d+)", f"\\g<1>{datetime.date.today().year}")

    if (not rmu.git_commit(config[rmu.k_repository_root], f"Set clientversion {config[rmu.k_version]} (build {config[rmu.k_build_number]})")):
        print("Commit failed")
        sys.exit(-1)

def update_checkpoints():
    # Get a mainnet and a testnet checkpoint candidate
    mainnet_checkpoint = get_checkpoint_candidate(rmu.k_mainnet, "https://explorer.horizen.io/")
    testnet_checkpoint = get_checkpoint_candidate(rmu.k_testnet, "https://explorer-testnet.horizen.io/")

    # Find the line numbers containing the last checkpoint
    # for both mainnet and testnet
    line_numbers = rmu.find_lines_containing(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), r'.*\(.*uint256S.*0x.*\).*\).*\,')
    if (len(line_numbers) < 2):
        print(f"Unable to find checkpoints lines in src/chainparams.cpp file")
        sys.exit(-1)

    for line_number in line_numbers[:2]:
        line = rmu.read_file_line(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_number)
        parts = line.rstrip().rsplit(",", 1)
        line = "".join(parts)
        rmu.replace_file_line(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_number, line)

    new_line = f"            ( {mainnet_checkpoint['height']}, uint256S(\"0x{mainnet_checkpoint['hash']}\")),\n"
    rmu.insert_line_into_file(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_numbers[0], new_line)

    # After the insertion, the line numbers are updated
    line_numbers[1] = line_numbers[1] + 1

    new_line = f"            ( {testnet_checkpoint['height']}, uint256S(\"0x{testnet_checkpoint['hash']}\")),\n"
    rmu.insert_line_into_file(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_numbers[1], new_line)

    line_numbers = rmu.find_lines_containing(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), ".*UNIX timestamp of last checkpoint block.*")
    if (len(line_numbers) < 2):
        print(f"Unable to find \"UNIX timestamp of last checkpoint block.\" lines in src/chainparams.cpp file")
        sys.exit(-1)

    line = rmu.read_file_line(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_numbers[0])
    line = re.sub(r"(\d+)", str(mainnet_checkpoint["time"]), line).rstrip()
    print(f"Substituted string {repr(line)}")
    rmu.replace_file_line(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_numbers[0], line)

    line = rmu.read_file_line(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_numbers[1])
    line = re.sub(r"(\d+)", str(testnet_checkpoint["time"]), line).rstrip()
    print(f"Substituted string {repr(line)}")
    rmu.replace_file_line(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_numbers[1], line)

    if (interactive):
        config[rmu.k_mainnet][rmu.k_total_transactions] = input(f"Please, enter the total number of transactions for MAINNET block {mainnet_checkpoint['height']}: ")
    line = rmu.read_file_line(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_numbers[0] + 1)
    line = re.sub(r"(\d+)", config[rmu.k_mainnet][rmu.k_total_transactions], line).rstrip()
    rmu.replace_file_line(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_numbers[0] + 1, line)
    
    if (interactive):
        config[rmu.k_testnet][rmu.k_total_transactions] = input(f"Please, enter the total number of transactions for TESTNET block {testnet_checkpoint['height']}: ")
    line = rmu.read_file_line(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_numbers[1] + 1)
    line = re.sub(r"(\d+)", config[rmu.k_testnet][rmu.k_total_transactions], line).rstrip()
    rmu.replace_file_line(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_numbers[1] + 1, line)

    average_mainnet_transactions = round(int(config[rmu.k_mainnet][rmu.k_total_transactions]) / (mainnet_checkpoint['height'] / (24 * 24)))
    line = rmu.read_file_line(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_numbers[0] + 3)
    line = re.sub(r"(\d+)", str(average_mainnet_transactions), line).rstrip()
    rmu.replace_file_line(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_numbers[0] + 3, line)

    average_testnet_transactions = round(int(config[rmu.k_testnet][rmu.k_total_transactions]) / (testnet_checkpoint['height'] / (24 * 24)))
    line = rmu.read_file_line(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_numbers[1] + 3)
    line = re.sub(r"(\d+)", str(average_testnet_transactions), line).rstrip()
    rmu.replace_file_line(os.path.join(config[rmu.k_repository_root], "src/chainparams.cpp"), line_numbers[1] + 3, line)

    if (not rmu.git_commit(config[rmu.k_repository_root], "Update checkpoint blocks")):
        print("Commit failed")
        sys.exit(-1)

def update_changelog():
    if (interactive):
        config[rmu.k_release_date] = input("Please, enter the new release date (e.g. Mon, 2 Jan 2023): ")
    release_date_split = config[rmu.k_release_date].split(" ")
    if (len(release_date_split) != 4 or
        release_date_split[0] not in {"Mon,", "Tue,", "Wed,", "Thu,", "Fri,", "Sat,", "Sun,"} or
        not release_date_split[1].isdigit() or
        release_date_split[2] not in {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"} or
        not release_date_split[3].isdigit()):
            print("Wrong release date; modify and retry.")
            sys.exit(-1)
    
    line_numbers = rmu.find_lines_containing(os.path.join(config[rmu.k_repository_root], "contrib/debian/changelog"), r'zen \(.*\).*')
    if (len(line_numbers) < 1):
        print(f"Unable to find line \"zen\" in contrib/debian/changelog file")
        sys.exit(-1)

    line = rmu.read_file_line(os.path.join(config[rmu.k_repository_root], "contrib/debian/changelog"), line_numbers[0])
    current_version = line[line.find("(")+1:line.find(")")]
    line = line.replace(current_version, f"{config[rmu.k_version]}{rmu.get_build_suffix(int(config[rmu.k_build_number]))}").rstrip()
    rmu.replace_file_line(os.path.join(config[rmu.k_repository_root], "contrib/debian/changelog"), line_numbers[0], line)

    line_numbers = rmu.find_lines_containing(os.path.join(config[rmu.k_repository_root], "contrib/debian/changelog"), '.* Zen Blockchain Foundation <info@horizen.io>  .*')
    if (len(line_numbers) < 1):
        print(f"Unable to find line \"-- Zen Blockchain Foundation <info@horizen.io>\" in contrib/debian/changelog file")
        sys.exit(-1)

    line = rmu.read_file_line(os.path.join(config[rmu.k_repository_root], "contrib/debian/changelog"), line_numbers[0])
    print(line)
    line = re.sub(r"\w{3}, \d{1,2} \w{3} \d{4}", config[rmu.k_release_date], line).rstrip()
    print(line)
    rmu.replace_file_line(os.path.join(config[rmu.k_repository_root], "contrib/debian/changelog"), line_numbers[0], line)

    if (not rmu.git_commit(config[rmu.k_repository_root], "Update Debian package info")):
        print("Commit failed")
        sys.exit(-1)

def update_deprecation_height():
    if (interactive):
        config[rmu.k_approx_release_height] = input("Enter the approximate release height (mainnet): ")
        config[rmu.k_weeks_until_deprecation] = input("Enter the weeks until deprecation: ")

    line_numbers = rmu.find_lines_containing(os.path.join(config[rmu.k_repository_root], "src/deprecation.h"), "static const int APPROX_RELEASE_HEIGHT.*=.*;")
    if (len(line_numbers) < 1):
        print(f"Unable to find \"static const int APPROX_RELEASE_HEIGHT\" line in src/deprecation.h file")
        sys.exit(-1)

    approx_release_height_line = rmu.read_file_line(os.path.join(config[rmu.k_repository_root], "src/deprecation.h"), line_numbers[0])
    approx_release_height_line = re.sub(r"(\d+)", str(config[rmu.k_approx_release_height]), approx_release_height_line).rstrip()
    rmu.replace_file_line(os.path.join(config[rmu.k_repository_root], "src/deprecation.h"), line_numbers[0], approx_release_height_line)

    line_numbers = rmu.find_lines_containing(os.path.join(config[rmu.k_repository_root], "src/deprecation.h"), "static const int WEEKS_UNTIL_DEPRECATION.*=.*;")
    if (len(line_numbers) < 1):
        print(f"Unable to find \"static const int WEEKS_UNTIL_DEPRECATION\" line in src/deprecation.h file")
        sys.exit(-1)

    weeks_until_deprecation_line = rmu.read_file_line(os.path.join(config[rmu.k_repository_root], "src/deprecation.h"), line_numbers[0])
    weeks_until_deprecation_line = re.sub(r"(\d+)", str(config[rmu.k_weeks_until_deprecation]), weeks_until_deprecation_line).rstrip()
    rmu.replace_file_line(os.path.join(config[rmu.k_repository_root], "src/deprecation.h"), line_numbers[0], weeks_until_deprecation_line)

    blocks_per_hour = 24
    blocks_per_day = 24 * blocks_per_hour
    blocks_per_week = 7 * blocks_per_day
    future_deprecation_height = int(config[rmu.k_approx_release_height]) + blocks_per_week * int(config[rmu.k_weeks_until_deprecation])

    # Get last block time
    last_block = rmu.get_last_blocks("https://explorer.horizen.io/")["blocks"][0]
    last_block_height = int(last_block["height"])
    last_block_timestamp = last_block["time"]
    last_block_date = datetime.datetime.fromtimestamp(last_block_timestamp)

    # Estimate the block time over the last 100'000 blocks
    reference_block = rmu.get_block_from_height("https://explorer.horizen.io/", last_block_height - 100000)
    reference_block_timestamp = reference_block["time"]
    block_time = (last_block_timestamp - reference_block_timestamp) / 100000

    # Estimate the datetime of the future_deprecation_height
    future_deprecation_date = last_block_date + datetime.timedelta(seconds=(future_deprecation_height - last_block_height) * block_time)

    # Estimate the next deprecation date
    date_string = future_deprecation_date.strftime('%Y-%m-%d')

    if (not rmu.git_commit(config[rmu.k_repository_root], f"Set deprecation height {future_deprecation_height}/{date_string}")):
        print("Commit failed")
        sys.exit(-1)

def build_zend():
    result_clean = subprocess.run(["make", "distclean"], cwd=config[rmu.k_repository_root])
    result_build = subprocess.run(["./zcutil/build.sh", "-j8"], cwd=config[rmu.k_repository_root])
    if (result_build.returncode != 0):
        print("Failure building zend")
        sys.exit(-1)

def update_man_pages():
    subprocess.run(["./contrib/devtools/gen-manpages.sh"], cwd=config[rmu.k_repository_root])
    if (not rmu.git_commit(config[rmu.k_repository_root], "Update man pages")):
        print("Commit failed")
        sys.exit(-1)

def update_release_notes():
    release_notes_file_path_src = os.path.join(config[rmu.k_repository_root], "doc/release-notes/release-notes-current.md")
    release_notes_file_path_dst = os.path.join(config[rmu.k_repository_root], f"doc/release-notes/release-notes-{config[rmu.k_version]}{rmu.get_build_suffix(int(config[rmu.k_build_number]))}.md")

    if (os.path.exists(release_notes_file_path_src)):
        shutil.copyfile(release_notes_file_path_src, release_notes_file_path_dst)
    else:
        print(f"Release notes file {release_notes_file_path_src} does not exist; please, check and retry.")
        sys.exit(-1)

    line_numbers = rmu.find_lines_containing(release_notes_file_path_dst, 'zend v.*')
    if (len(line_numbers) < 1):
        print(f"Unable to find line \"zend\" in {release_notes_file_path_dst} file")
        sys.exit(-1)

    line = rmu.read_file_line(release_notes_file_path_dst, line_numbers[0])
    current_version = line[line.find("zend v")+len("zend v"):len(line)-1]
    line = line.replace(current_version, f"{config[rmu.k_version]}{rmu.get_build_suffix(int(config[rmu.k_build_number]))}").rstrip()
    rmu.replace_file_line(release_notes_file_path_dst, line_numbers[0], line)

    if (not rmu.git_commit(config[rmu.k_repository_root], "Add release notes", ["doc/release-notes"])):
        print("Commit failed")
        sys.exit(-1)

def check_dependencies():
    result_build = subprocess.run(["make", "-C", "depends", "download"], cwd=config[rmu.k_repository_root])
    if (result_build.returncode != 0):
        print("Failure building dependencies")
        sys.exit(-1)

    result_availability = subprocess.run(["./qa/zen/test-depends-sources-mirror.py"], cwd=config[rmu.k_repository_root])
    if (result_availability.returncode != 0):
        print("Failure checking depends availability")
        sys.exit(-1)


# ---------- SCRIPT MAIN ---------- #

config = {}
interactive = True

print("\n********** Step 0: setup config **********\n")
initialize()
print("Done")

print("\n********** Step 1: set the new client version **********\n")
if (interactive):
    rmu.ask_for_step_skip(rmu.k_set_client_version)
if (not bool(config[rmu.k_script_steps][rmu.k_set_client_version][rmu.k_skip])):
    if (bool(config[rmu.k_script_steps][rmu.k_set_client_version][rmu.k_stop])):
        input("Press a key to proceed")
    set_client_version()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 2: update the mainnet and testnet checkpoints **********\n")
if (interactive):
    rmu.ask_for_step_skip(rmu.k_update_checkpoints)
if (not bool(config[rmu.k_script_steps][rmu.k_update_checkpoints][rmu.k_skip])):
    if (bool(config[rmu.k_script_steps][rmu.k_update_checkpoints][rmu.k_stop])):
        input("Press a key to proceed")
    update_checkpoints()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 3: update changelog **********\n")
if (interactive):
    rmu.ask_for_step_skip(rmu.k_update_changelog)
if (not bool(config[rmu.k_script_steps][rmu.k_update_changelog][rmu.k_skip])):
    if (bool(config[rmu.k_script_steps][rmu.k_update_changelog][rmu.k_stop])):
        input("Press a key to proceed")
    update_changelog()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 4: update deprecation height **********\n")
if (interactive):
    rmu.ask_for_step_skip(rmu.k_update_deprecation_height)
if (not bool(config[rmu.k_script_steps][rmu.k_update_deprecation_height][rmu.k_skip])):
    if (bool(config[rmu.k_script_steps][rmu.k_update_deprecation_height][rmu.k_stop])):
        input("Press a key to proceed")
    update_deprecation_height()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 5: build Zend **********\n")
if (interactive):
    rmu.ask_for_step_skip(rmu.k_build_zend)
if (not bool(config[rmu.k_script_steps][rmu.k_build_zend][rmu.k_skip])):
    if (bool(config[rmu.k_script_steps][rmu.k_build_zend][rmu.k_stop])):
        input("Press a key to proceed")
    build_zend()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 6: update man pages **********\n")
if (interactive):
    rmu.ask_for_step_skip(rmu.k_update_man_pages)
if (not bool(config[rmu.k_script_steps][rmu.k_update_man_pages][rmu.k_skip])):
    if (bool(config[rmu.k_script_steps][rmu.k_update_man_pages][rmu.k_stop])):
        input("Press a key to proceed")
    update_man_pages()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 7: update release_notes **********\n")
if (interactive):
    rmu.ask_for_step_skip(rmu.k_update_release_notes)
if (not bool(config[rmu.k_script_steps][rmu.k_update_release_notes][rmu.k_skip])):
    if (bool(config[rmu.k_script_steps][rmu.k_update_release_notes][rmu.k_stop])):
        input("Press a key to proceed")
    update_release_notes()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 8: check dependencies **********\n")
if (interactive):
    rmu.ask_for_step_skip(rmu.k_check_dependencies)
if (not bool(config[rmu.k_script_steps][rmu.k_check_dependencies][rmu.k_skip])):
    if (bool(config[rmu.k_script_steps][rmu.k_check_dependencies][rmu.k_stop])):
        input("Press a key to proceed")
    check_dependencies()
    print("Done")
else:
    print("Skipped")

print("Script execution completed")
