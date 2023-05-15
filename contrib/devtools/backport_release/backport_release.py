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
k_build_number = "build_number"
k_is_official_version = "is_official_version"
k_script_steps = "script_steps"
k_initialize_check_main_checked_out = "initialize_check_main_checked_out"
k_initialize_check_no_pending_changes = "initialize_check_no_pending_changes"
k_initialize_create_branch = "initialize_create_branch"
k_initialize_merge_branch = "initialize_merge_branch"
k_reset_client_version = "reset_client_version"
k_reset_checkpoints = "reset_checkpoints"
k_reset_changelog = "reset_changelog"
k_build_zend = "build_zend"
k_reset_man_pages = "reset_man_pages"
k_reset_release_notes = "reset_release_notes"
k_reset_readme = "reset_readme"
k_skip = "skip"
k_stop = "stop"


# git functions
def git_check_currently_main_checked_out():
    result = subprocess.run(["git", "rev-parse", "--abbrev-ref", "HEAD"], capture_output=True, text=True, cwd=config[k_repository_root])
    return result.returncode == 0 and result.stdout.rstrip() == "main"

def git_check_pending_changes():
    result = subprocess.run(["git", "diff"], capture_output=True, text=True, cwd=config[k_repository_root])
    return result.returncode == 0 and result.stdout != ""

def git_create_branch(branch_name: str):
   result = subprocess.run(["git", "rev-parse", "--verify", branch_name], capture_output=True, text=True, cwd=config[k_repository_root])
   if (result.returncode == 0):
        print("Branch already exist; check if script has been already launched or if provided new version is wrong")
        sys.exit(-1)
   result = subprocess.run(["git", "checkout", "-b", branch_name], capture_output=True, text=True, cwd=config[k_repository_root])
   if (result.returncode != 0):
        print("Branch creation failed")
        sys.exit(-1)

def git_merge_no_commit(branch_to_merge: str):
    result_merge = subprocess.run(["git", "merge", branch_to_merge, "--no-commit", "--no-ff"], capture_output=True, text=True, cwd=config[k_repository_root])
    if (result_merge.returncode != 0):
        print(f"Merge failed (details: {result_merge.stdout})") # in case of merge conflicts the message is contained in stdout (not stderr)
        input("Press a key to proceed")

def git_reset_file(file_to_reset: str):
    result_reset = subprocess.run(["git", "checkout", file_to_reset], capture_output=True, text=True, cwd=config[k_repository_root])
    if (result_reset.returncode != 0):
        print("Changes discard failed")
        sys.exit(-1)

def git_commit(commit_title: str, directories_for_add: list[str] = []):
    for directory_for_add in directories_for_add:
        subprocess.run(["git", "add", directory_for_add], cwd=config[k_repository_root]) # Add changes to the index
    result = subprocess.run(["git", "commit", "-a", "-S", "-m", commit_title], capture_output=True, text=True, cwd=config[k_repository_root]) # Commit changes with the given message
    if (result.returncode != 0 or git_check_pending_changes()):
        print("Commit failed")
        sys.exit(-1)


# utility functions
def get_version_string_details(version_string: str):
    assert(version_string.count(".") == 2)
    digits = [int(digit) for digit in version_string.split(sep='.') if digit.isdigit()]
    assert(len(digits) == 3)
    return digits

def get_build_suffix(build_number: int):
    if (1 <= build_number and build_number <= 24):
        return f"-beta{build_number}"
    if (25 <= build_number and build_number <= 49):
        return f"-rc{build_number - 24}"
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
    
    if (interactive):
        ask_for_step_skip(k_initialize_check_main_checked_out)
    if (not bool(config[k_script_steps][k_initialize_check_main_checked_out][k_skip])):
        if (bool(config[k_script_steps][k_initialize_check_main_checked_out][k_stop])):
            input("Press a key to proceed")
        if (not git_check_currently_main_checked_out()):
            print("Currently selected branch is not \"main\"; checkout \"main\" branch and retry.")
            sys.exit(-1)

    if (interactive):
        ask_for_step_skip(k_initialize_check_no_pending_changes)
    if (not bool(config[k_script_steps][k_initialize_check_no_pending_changes][k_skip])):
        if (bool(config[k_script_steps][k_initialize_check_no_pending_changes][k_stop])):
            input("Press a key to proceed")
        if (git_check_pending_changes()):
            print("There are pending changes in selected repository; commit or stash them and retry.")
            sys.exit(-1)

    if (interactive):
        config[k_version] = input("Enter the backported version (format major.minor.patch, e.g. 3.3.1): ")
        print("Enter the backported build number:")
        print("Please, use the following values:")
        print("[ 1, 24] when releasing a beta version (e.g. 3.3.1-beta1, 3.3.1-beta2, etc.)")
        print("[25, 49] when releasing a release candidate (e.g. 3.3.1-rc1, 3.3.1-rc2, etc.)")
        print("    [50] when making an official release (e.g. 3.3.1)")
        config[k_build_number] = input("")
    config[k_is_official_version] = (config[k_build_number] == "50")

    build_number = int(config[k_build_number])
    if (1 > build_number or build_number > 50):
        print("Wrong build number; modify and retry.")
        sys.exit(-1)

    if (interactive):
        ask_for_step_skip(k_initialize_create_branch)
    if (not bool(config[k_script_steps][k_initialize_create_branch][k_skip])):
        if (bool(config[k_script_steps][k_initialize_create_branch][k_stop])):
            input("Press a key to proceed")
        prepare_release_branch_name = f"backport_release/{config[k_version]}{get_build_suffix(build_number)}"
        git_create_branch(prepare_release_branch_name)

    if (interactive):
        ask_for_step_skip(k_initialize_merge_branch)
    if (not bool(config[k_script_steps][k_initialize_merge_branch][k_skip])):
        if (bool(config[k_script_steps][k_initialize_merge_branch][k_stop])):
            input("Press a key to proceed")
    release_branch_name = f"release/{config[k_version]}{get_build_suffix(build_number)}"
    git_merge_no_commit(release_branch_name)

    return config

def reset_client_version():
    version_digits = get_version_string_details(config[k_version])

    rep_maj = replace_string_in_file(os.path.join(config[k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_MAJOR, (\d+)\)",    f"define(_CLIENT_VERSION_MAJOR, {version_digits[0]})")
    rep_min = replace_string_in_file(os.path.join(config[k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_MINOR, (\d+)\)",    f"define(_CLIENT_VERSION_MINOR, {version_digits[1]})")
    rep_rev = replace_string_in_file(os.path.join(config[k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_REVISION, (\d+)\)", f"define(_CLIENT_VERSION_REVISION, {99})")
    rep_bld = replace_string_in_file(os.path.join(config[k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_BUILD, (\d+)\)",    f"define(_CLIENT_VERSION_BUILD, {0})")
    if (not rep_maj and not rep_min and not rep_rev and not rep_bld):
        print("Version replacement failed (configure.ac)")
        sys.exit(-1)
    replace_string_in_file(os.path.join(config[k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_IS_RELEASE, .*\)", "define(_CLIENT_VERSION_IS_RELEASE, false)")
    replace_string_in_file(os.path.join(config[k_repository_root], "configure.ac"), r"define\(_COPYRIGHT_YEAR, (\d+)\)", f"define(_COPYRIGHT_YEAR, {datetime.date.today().year})")

    rep_maj = replace_string_in_file(os.path.join(config[k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_MAJOR\s+)(\d+)",    f"\\g<1>{version_digits[0]}")
    rep_min = replace_string_in_file(os.path.join(config[k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_MINOR\s+)(\d+)",    f"\\g<1>{version_digits[1]}")
    rep_rev = replace_string_in_file(os.path.join(config[k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_REVISION\s+)(\d+)", f"\\g<1>{99}")
    rep_bld = replace_string_in_file(os.path.join(config[k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_BUILD\s+)(\d+)",    f"\\g<1>{0}")
    if (not rep_maj and not rep_min and not rep_rev and not rep_bld):
        print("Version replacement failed (src/clientversion.h)")
        sys.exit(-1)
    replace_string_in_file(os.path.join(config[k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_IS_RELEASE\s+)(\w+)", "\\g<1>false")
    replace_string_in_file(os.path.join(config[k_repository_root], "src/clientversion.h"), r"(#define\s+COPYRIGHT_YEAR\s+)(\d+)", f"\\g<1>{datetime.date.today().year}")

def reset_changelog():
    version_digits = get_version_string_details(config[k_version])
    
    line_numbers = find_lines_containing(os.path.join(config[k_repository_root], "contrib/debian/changelog"), 'zen \(.*\).*')
    if (len(line_numbers) < 1):
        print(f"Unable to find line \"zen\" in contrib/debian/changelog file")
        sys.exit(-1)

    line = read_file_line(os.path.join(config[k_repository_root], "contrib/debian/changelog"), line_numbers[0])
    current_version = line[line.find("(")+1:line.find(")")]
    line = line.replace(current_version, f"{version_digits[0]}.{version_digits[1]}.99").rstrip()
    replace_file_line(os.path.join(config[k_repository_root], "contrib/debian/changelog"), line_numbers[0], line)

def reset_man_pages():
    man_pages_zen_cli = os.path.join(config[k_repository_root], "doc/man/zen-cli.1")
    man_pages_zen_tx = os.path.join(config[k_repository_root], "doc/man/zen-tx.1")
    man_pages_zend = os.path.join(config[k_repository_root], "doc/man/zend.1")

    with open(man_pages_zen_cli, "w") as file:
        file.writelines(".TH ZEN-CLI \"1\"\n")
        file.writelines(".SH NAME\n")
        file.writelines("zen-cli \- manual page for zen-cli\n")
        file.writelines("\n")
        file.writelines("This is a placeholder file. Please follow the instructions in \\fIcontrib/devtools/README.md\\fR to generate the manual pages after a release.\n")
    
    with open(man_pages_zen_tx, "w") as file:
        file.writelines(".TH ZEN-TX \"1\"\n")
        file.writelines(".SH NAME\n")
        file.writelines("zen-tx \- manual page for zen-tx\n")
        file.writelines("\n")
        file.writelines("This is a placeholder file. Please follow the instructions in \\fIcontrib/devtools/README.md\\fR to generate the manual pages after a release.\n")

    with open(man_pages_zend, "w") as file:
        file.writelines(".TH ZEND \"1\"\n")
        file.writelines(".SH NAME\n")
        file.writelines("zend \- manual page for zend\n")
        file.writelines("\n")
        file.writelines("This is a placeholder file. Please follow the instructions in \\fIcontrib/devtools/README.md\\fR to generate the manual pages after a release.\n")

def reset_release_notes():
    release_notes_file_path_src = os.path.join(config[k_repository_root], "doc/release-notes/release-notes-current.md")
    with open(release_notes_file_path_src, "w") as file:
        version_digits = get_version_string_details(config[k_version])
        file.writelines(f"zend v{version_digits[0]}.{version_digits[1]}.99\n")
        file.writelines("=========\n")
        file.writelines("\n")
        file.writelines("## Important notes\n")
        file.writelines("\n")
        file.writelines("## New Features and Improvements\n")
        file.writelines("\n")
        file.writelines("## Bugfixes and Minor Changes\n")
        file.writelines("\n")
        file.writelines("## Contributors\n")
        file.writelines("\n")

def reset_readme():
    version_digits = get_version_string_details(config[k_version])

    line_numbers = find_lines_containing(os.path.join(config[k_repository_root], "README.md"), 'Zend .*')
    if (len(line_numbers) < 1):
        print(f"Unable to find \"Zend\" line in README.md file")
        sys.exit(-1)

    replace_file_line(os.path.join(config[k_repository_root], "README.md"), line_numbers[0], f"Zend {version_digits[0]}.{version_digits[1]}.99".rstrip())

def build_zend():
    result_clean = subprocess.run(["make", "distclean"], cwd=config[k_repository_root])
    result_build = subprocess.run(["./zcutil/build.sh", "-j8"], cwd=config[k_repository_root])
    if (result_build.returncode != 0):
        print("Failure building zend")
        sys.exit(-1)


config = {}
interactive = True

print("\n********** Step 0: setup config **********\n")
initialize()

print("\n********** Step 1: reset the backported client version **********\n")
if (interactive):
    config[k_script_steps] = {}
    ask_for_step_skip(k_reset_client_version)
if (not bool(config[k_script_steps][k_reset_client_version][k_skip])):
    if (bool(config[k_script_steps][k_reset_client_version][k_stop])):
        input("Press a key to proceed")
    reset_client_version()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 2: reset the mainnet and testnet checkpoints **********\n")
if (not config[k_is_official_version]):
    if (interactive):
        ask_for_step_skip(k_reset_checkpoints)
    if (not bool(config[k_script_steps][k_reset_checkpoints][k_skip])):
        if (bool(config[k_script_steps][k_reset_checkpoints][k_stop])):
            input("Press a key to proceed")
        git_reset_file(os.path.join(config[k_repository_root], "src/chainparams.cpp"))
        print("Done")
    else:
        print("Skipped")
else:
    print("Must not be done")

print("\n********** Step 3: reset changelog **********\n")
if (interactive):
    ask_for_step_skip(k_reset_changelog)
if (not bool(config[k_script_steps][k_reset_changelog][k_skip])):
    if (bool(config[k_script_steps][k_reset_changelog][k_stop])):
        input("Press a key to proceed")
    reset_changelog()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 4: build Zend **********\n")
if (interactive):
    ask_for_step_skip(k_build_zend)
if (not bool(config[k_script_steps][k_build_zend][k_skip])):
    if (bool(config[k_script_steps][k_build_zend][k_stop])):
        input("Press a key to proceed")
    build_zend()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 5: reset man pages **********\n")
if (interactive):
    ask_for_step_skip(k_reset_man_pages)
if (not bool(config[k_script_steps][k_reset_man_pages][k_skip])):
    if (bool(config[k_script_steps][k_reset_man_pages][k_stop])):
        input("Press a key to proceed")
    reset_man_pages()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 6: reset release_notes **********\n")
if (config[k_is_official_version]):
    if (interactive):
        ask_for_step_skip(k_reset_release_notes)
    if (not bool(config[k_script_steps][k_reset_release_notes][k_skip])):
        if (bool(config[k_script_steps][k_reset_release_notes][k_stop])):
            input("Press a key to proceed")
        reset_release_notes()
        print("Done")
    else:
        print("Skipped")
else:
    print("Must not be done")

print("\n********** Step 7: reset readme **********\n")
if (interactive):
    ask_for_step_skip(k_reset_readme)
if (not bool(config[k_script_steps][k_reset_readme][k_skip])):
    if (bool(config[k_script_steps][k_reset_readme][k_stop])):
        input("Press a key to proceed")
    reset_readme()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 8: commit **********\n")
git_commit(f"Release {config[k_version]}{get_build_suffix(int(config[k_build_number]))} backport", [os.path.join(config[k_repository_root], "contrib/devtools/backport_release")])
print("Done")