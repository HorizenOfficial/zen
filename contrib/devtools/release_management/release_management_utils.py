#!/usr/bin/python3

import subprocess
import re
import urllib.request
import json


# dictionary keys
k_repository_root = "repository_root"
k_version = "version"
k_build_number = "build_number"
k_is_official_version = "is_official_version"
k_mainnet = "mainnet"
k_testnet = "testnet"
k_checkpoint_height = "checkpoint_height"
k_total_transactions = "total_transactions"
k_release_date = "release_date"
k_approx_release_height = "approx_release_height"
k_weeks_until_deprecation = "weeks_until_deprecation"
k_script_steps = "script_steps"
k_initialize_check_main_checked_out = "initialize_check_main_checked_out"
k_initialize_check_no_pending_changes = "initialize_check_no_pending_changes"
k_initialize_create_branch = "initialize_create_branch"
k_initialize_merge_branch = "initialize_merge_branch"
k_set_client_version = "set_client_version"
k_reset_client_version = "reset_client_version"
k_update_checkpoints = "update_checkpoints"
k_reset_checkpoints = "reset_checkpoints"
k_update_changelog = "update_changelog"
k_reset_changelog = "reset_changelog"
k_update_deprecation_height = "update_deprecation_height"
k_build_zend = "build_zend"
k_update_man_pages = "update_man_pages"
k_reset_man_pages = "reset_man_pages"
k_update_release_notes = "update_release_notes"
k_reset_release_notes = "reset_release_notes"
k_check_dependencies = "check_dependencies"
k_skip = "skip"
k_stop = "stop"


# git functions
def git_check_currently_main_checked_out(cwd: str):
    result = subprocess.run(["git", "rev-parse", "--abbrev-ref", "HEAD"], capture_output=True, text=True, cwd=cwd)
    return result.returncode == 0 and result.stdout.rstrip() == "main"

def git_check_pending_changes(cwd: str):
    result = subprocess.run(["git", "diff"], capture_output=True, text=True, cwd=cwd)
    return result.returncode == 0 and result.stdout != ""

def git_check_branch_existence(cwd: str, branch_name: str):
   result = subprocess.run(["git", "rev-parse", "--verify", branch_name], capture_output=True, text=True, cwd=cwd)
   return result.returncode == 0

def git_create_branch(cwd: str, branch_name: str):
   result = subprocess.run(["git", "checkout", "-b", branch_name], capture_output=True, text=True, cwd=cwd)
   return result.returncode == 0

def git_commit(cwd: str, commit_title: str, directories_for_add: list[str] = []):
    for directory_for_add in directories_for_add:
        subprocess.run(["git", "add", directory_for_add], cwd=cwd) # Add changes to the index
    result = subprocess.run(["git", "commit", "-a", "-S", "-m", commit_title], capture_output=True, text=True, cwd=cwd) # Commit changes with the given message
    return result.returncode == 0 and not git_check_pending_changes(cwd)

def git_reset_file(cwd: str, file_to_reset: str):
    result_reset = subprocess.run(["git", "checkout", file_to_reset], capture_output=True, text=True, cwd=cwd)
    return result_reset.returncode == 0

def git_merge_no_commit(cwd: str, branch_to_merge: str):
    result_merge = subprocess.run(["git", "merge", branch_to_merge, "--no-commit", "--no-ff"], capture_output=True, text=True, cwd=cwd)
    return result_merge.returncode == 0, result_merge.stdout, result_merge.stderr


# generic utility functions
def get_version_string_details(version_string: str):
    assert(version_string.count(".") == 2)
    digits = [int(digit) for digit in version_string.split(sep='.') if digit.isdigit()]
    assert(len(digits) == 3)
    return digits

def get_build_suffix(build_number: int):
    if (0 <= build_number and build_number <= 24):
        return f"-beta{build_number + 1}"
    if (25 <= build_number and build_number <= 49):
        return f"-rc{build_number - 24}"
    if (build_number == 50):
        return ""
    if (51 <= build_number):
        return f"-alpha{build_number - 50}"
    return ""

def replace_string_in_file(filepath: str, old_string_regex: str, new_string: str, ):
    replacement_occurred = False

    with open(filepath, "r") as file:
        file_data = file.read()

    new_data = re.sub(old_string_regex, new_string, file_data)

    if (new_data != file_data):
        with open(filepath, "w") as file:
            file.write(new_data)
        replacement_occurred = True
    
    return replacement_occurred

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

def find_lines_containing(file_path: str, pattern_to_find: str):
    line_numbers = []

    with open(file_path, "r") as file:
        # Read all lines of the file into a list
        lines = file.readlines()

        # Find the lines containing the string to find
        pattern = re.compile(pattern_to_find)
        line_numbers = [i for i, line in enumerate(lines) if pattern.match(line) is not None]

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

def ask_for_step_skip(script_step: str, config: dict, step_details: str = ""):
    if (not k_script_steps in config):
        config[k_script_steps] = {}
    if (not script_step in config[k_script_steps]):
        config[k_script_steps][script_step] = {}
        config[k_script_steps][script_step][k_stop] = False
    if (step_details != "" and not step_details.startswith("(")):
        step_details = " (" + step_details
    if (step_details != "" and not step_details.endswith(")")):
        step_details = step_details + ")"
    if (input(f"Do you want to skip this step{step_details}? (Y/N)").upper() == "Y"):
        config[k_script_steps][script_step][k_skip] = True
    else:
        config[k_script_steps][script_step][k_skip] = False
