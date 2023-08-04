#!/usr/bin/python3

import datetime
import os
import subprocess
import yaml
import sys
import requests
import release_management_utils as rmu


# script steps functions
def initialize():
    global config
    global config_file
    global interactive
    
    config_file = ""
    if (len(sys.argv) > 1):
        config_file = sys.argv[1]
    else:
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
        config[rmu.k_repository_root] = input("Enter the repository root path: ")
    
    if (interactive):
        rmu.ask_for_step_skip(rmu.k_initialize_check_main_checked_out)
    if (not bool(config[rmu.k_script_steps][rmu.k_initialize_check_main_checked_out][rmu.k_skip])):
        if (bool(config[rmu.k_script_steps][rmu.k_initialize_check_main_checked_out][rmu.k_stop])):
            input("Press a key to proceed")
        if (not rmu.git_check_currently_main_checked_out(config[rmu.k_repository_root])):
            print("Currently selected branch is not \"main\"; checkout \"main\" branch and retry.")
            sys.exit(-1)

    if (interactive):
        rmu.ask_for_step_skip(rmu.k_initialize_check_no_pending_changes)
    if (not bool(config[rmu.k_script_steps][rmu.k_initialize_check_no_pending_changes][rmu.k_skip])):
        if (bool(config[rmu.k_script_steps][rmu.k_initialize_check_no_pending_changes][rmu.k_stop])):
            input("Press a key to proceed")
        if (rmu.git_check_pending_changes(config[rmu.k_repository_root])):
            print("There are pending changes in selected repository; commit or stash them and retry.")
            sys.exit(-1)

    if (interactive):
        config[rmu.k_version] = input("Enter the backported version (format major.minor.patch, e.g. 3.3.1): ")
        print("Enter the backported build number:")
        print("Please, use the following values:")
        print("[ 0, 24] when backporting a beta version (e.g. 3.3.1-beta1, 3.3.1-beta2, etc.)")
        print("[25, 49] when backporting a release candidate (e.g. 3.3.1-rc1, 3.3.1-rc2, etc.)")
        print("    [50] when backporting an official release (e.g. 3.3.1)")
        print("   [51+] when backporting an alpha version (e.g. 3.3.1-alpha1, 3.3.1-alpha2, etc.)")
        config[rmu.k_build_number] = input("")
    config[rmu.k_is_official_version] = (config[rmu.k_build_number] == "50")

    build_number = int(config[rmu.k_build_number])
    if (0 > build_number):
        print("Wrong build number; modify and retry.")
        sys.exit(-1)

    if (interactive):
        rmu.ask_for_step_skip(rmu.k_initialize_create_branch)
    if (not bool(config[rmu.k_script_steps][rmu.k_initialize_create_branch][rmu.k_skip])):
        if (bool(config[rmu.k_script_steps][rmu.k_initialize_create_branch][rmu.k_stop])):
            input("Press a key to proceed")
        backport_release_branch_name = f"backport_release/{config[rmu.k_version]}{rmu.get_build_suffix(build_number)}"       
        if (rmu.git_check_branch_existence(config[rmu.k_repository_root], backport_release_branch_name)):
            print("Branch already exists; check if script has been already launched or if provided new version is wrong")
            sys.exit(-1)
        if (not rmu.git_create_branch(config[rmu.k_repository_root], backport_release_branch_name)):
            print("Branch creation failed")
            sys.exit(-1)

    if (interactive):
        rmu.ask_for_step_skip(rmu.k_initialize_merge_branch)
    if (not bool(config[rmu.k_script_steps][rmu.k_initialize_merge_branch][rmu.k_skip])):
        if (bool(config[rmu.k_script_steps][rmu.k_initialize_merge_branch][rmu.k_stop])):
            input("Press a key to proceed")
    release_branch_name = f"release/{config[rmu.k_version]}{rmu.get_build_suffix(build_number)}"
    result_merge, details_merge_stdout, details_merge_stderr = rmu.git_merge_no_commit(config[rmu.k_repository_root], release_branch_name)
    if (not result_merge):
        print(f"Merge failed (details:\n{details_merge_stdout}\n{details_merge_stderr})")
        input("Press a key to proceed")

    return config

def reset_client_version():
    version_digits = rmu.get_version_string_details(config[rmu.k_version])

    rep_maj = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_MAJOR, (\d+)\)",    f"define(_CLIENT_VERSION_MAJOR, {version_digits[0]})")
    rep_min = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_MINOR, (\d+)\)",    f"define(_CLIENT_VERSION_MINOR, {version_digits[1]})")
    rep_rev = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_REVISION, (\d+)\)", f"define(_CLIENT_VERSION_REVISION, {99})")
    rep_bld = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_BUILD, (\d+)\)",    f"define(_CLIENT_VERSION_BUILD, {51})")
    if (not rep_maj and not rep_min and not rep_rev and not rep_bld):
        print("Version replacement failed (configure.ac)")
        sys.exit(-1)
    rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "configure.ac"), r"define\(_CLIENT_VERSION_IS_RELEASE, .*\)", "define(_CLIENT_VERSION_IS_RELEASE, false)")
    rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "configure.ac"), r"define\(_COPYRIGHT_YEAR, (\d+)\)", f"define(_COPYRIGHT_YEAR, {datetime.date.today().year})")

    rep_maj = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_MAJOR\s+)(\d+)",    f"\\g<1>{version_digits[0]}")
    rep_min = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_MINOR\s+)(\d+)",    f"\\g<1>{version_digits[1]}")
    rep_rev = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_REVISION\s+)(\d+)", f"\\g<1>{99}")
    rep_bld = rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_BUILD\s+)(\d+)",    f"\\g<1>{51}")
    if (not rep_maj and not rep_min and not rep_rev and not rep_bld):
        print("Version replacement failed (src/clientversion.h)")
        sys.exit(-1)
    rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "src/clientversion.h"), r"(#define\s+CLIENT_VERSION_IS_RELEASE\s+)(\w+)", "\\g<1>false")
    rmu.replace_string_in_file(os.path.join(config[rmu.k_repository_root], "src/clientversion.h"), r"(#define\s+COPYRIGHT_YEAR\s+)(\d+)", f"\\g<1>{datetime.date.today().year}")

def reset_changelog():
    version_digits = rmu.get_version_string_details(config[rmu.k_version])
    
    line_numbers = rmu.find_lines_containing(os.path.join(config[rmu.k_repository_root], "contrib/debian/changelog"), 'zen \(.*\).*')
    if (len(line_numbers) < 1):
        print(f"Unable to find line \"zen\" in contrib/debian/changelog file")
        sys.exit(-1)

    line = rmu.read_file_line(os.path.join(config[rmu.k_repository_root], "contrib/debian/changelog"), line_numbers[0])
    current_version = line[line.find("(")+1:line.find(")")]
    line = line.replace(current_version, f"{version_digits[0]}.{version_digits[1]}.99").rstrip()
    rmu.replace_file_line(os.path.join(config[rmu.k_repository_root], "contrib/debian/changelog"), line_numbers[0], line)

def reset_man_pages():
    man_pages_zen_cli = os.path.join(config[rmu.k_repository_root], "doc/man/zen-cli.1")
    man_pages_zen_tx = os.path.join(config[rmu.k_repository_root], "doc/man/zen-tx.1")
    man_pages_zend = os.path.join(config[rmu.k_repository_root], "doc/man/zend.1")

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
    release_notes_file_path_current = os.path.join(config[rmu.k_repository_root], "doc/release-notes/release-notes-current.md")
    release_notes_file_path_backport = os.path.join(config[rmu.k_repository_root], f"doc/release-notes/release-notes-{config[rmu.k_version]}{rmu.get_build_suffix(int(config[rmu.k_build_number]))}.md")

    # keep from "## Important Notes\n" (included) to "## Contributors\n" (included)
    with open(release_notes_file_path_current, "r") as file:
        release_notes_lines_current = file.readlines()
    while len(release_notes_lines_current) > 0:
        del release_notes_lines_current[0]
        if (len(release_notes_lines_current) > 0 and
            (release_notes_lines_current[0] == "## Important Notes\n" or
             release_notes_lines_current[0] == "## New Features and Improvements\n" or
             release_notes_lines_current[0] == "## Bugfixes and Minor Changes\n" or
             release_notes_lines_current[0] == "## Contributors\n")):
            break
    while len(release_notes_lines_current) > 0:
        if (release_notes_lines_current[len(release_notes_lines_current) - 1] == "## Contributors\n"):
            break
        del release_notes_lines_current[len(release_notes_lines_current) - 1]

    with open(release_notes_file_path_backport, "r") as file:
        release_notes_lines_backport = file.readlines()

    contributors = {}
    with open(release_notes_file_path_current, "w") as file:
        version_digits = rmu.get_version_string_details(config[rmu.k_version])
        file.writelines(f"zend v{version_digits[0]}.{version_digits[1]}.99\n")
        file.writelines("=========\n")
        file.writelines("\n")
        for line_index in range(len(release_notes_lines_current)):
            if (release_notes_lines_current[line_index] == "\n" or
                release_notes_lines_current[line_index].startswith("##")):
                file.writelines(release_notes_lines_current[line_index])
            elif (release_notes_lines_current[line_index] not in release_notes_lines_backport):
                file.writelines(release_notes_lines_current[line_index])
                PR_from_idx = release_notes_lines_current[line_index].find("[#")
                PR_to_idx = release_notes_lines_current[line_index].find("]")
                PR = release_notes_lines_current[line_index][PR_from_idx+2:PR_to_idx]
                if (PR.isdigit()):
                    pull_request_url = f"https://api.github.com/repos/HorizenOfficial/zen/pulls/{PR}"
                    url = pull_request_url.format(owner="HorizenOfficial", repo="zen", pull_request_number=PR)
                    response = requests.get(url)
                    if response.status_code == 200:
                        pr_data = response.json()
                        contributors[pr_data["user"]["login"]] = pr_data["user"]["html_url"]
        for contrib_key in contributors:
            file.writelines(f"[@{contrib_key}]({contributors[contrib_key]})\n")

def build_zend():
    result_clean = subprocess.run(["make", "distclean"], cwd=config[rmu.k_repository_root])
    result_build = subprocess.run(["./zcutil/build.sh", "-j8"], cwd=config[rmu.k_repository_root])
    if (result_build.returncode != 0):
        print("Failure building zend")
        sys.exit(-1)


config = {}
config_file = ""
interactive = True

print("\n********** Step 0: setup config **********\n")
initialize()

print("\n********** Step 1: reset the backported client version **********\n")
if (interactive):
    config[rmu.k_script_steps] = {}
    rmu.ask_for_step_skip(rmu.k_reset_client_version)
if (not bool(config[rmu.k_script_steps][rmu.k_reset_client_version][rmu.k_skip])):
    if (bool(config[rmu.k_script_steps][rmu.k_reset_client_version][rmu.k_stop])):
        input("Press a key to proceed")
    reset_client_version()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 2: reset the mainnet and testnet checkpoints **********\n")
if (not config[rmu.k_is_official_version]):
    if (interactive):
        rmu.ask_for_step_skip(rmu.k_reset_checkpoints)
    if (not bool(config[rmu.k_script_steps][rmu.k_reset_checkpoints][rmu.k_skip])):
        if (bool(config[rmu.k_script_steps][rmu.k_reset_checkpoints][rmu.k_stop])):
            input("Press a key to proceed")
        if (not rmu.git_reset_file(config[rmu.k_repository_root], "src/chainparams.cpp")):
            print("Changes discarding failed")
            sys.exit(-1)
        print("Done")
    else:
        print("Skipped")
else:
    print("Must not be done")

print("\n********** Step 3: reset changelog **********\n")
if (interactive):
    rmu.ask_for_step_skip(rmu.k_reset_changelog)
if (not bool(config[rmu.k_script_steps][rmu.k_reset_changelog][rmu.k_skip])):
    if (bool(config[rmu.k_script_steps][rmu.k_reset_changelog][rmu.k_stop])):
        input("Press a key to proceed")
    reset_changelog()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 4: build Zend **********\n")
if (interactive):
    rmu.ask_for_step_skip(rmu.k_build_zend)
if (not bool(config[rmu.k_script_steps][rmu.k_build_zend][rmu.k_skip])):
    if (bool(config[rmu.k_script_steps][rmu.k_build_zend][rmu.k_stop])):
        input("Press a key to proceed")
    build_zend()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 5: reset man pages **********\n")
if (interactive):
    rmu.ask_for_step_skip(rmu.k_reset_man_pages)
if (not bool(config[rmu.k_script_steps][rmu.k_reset_man_pages][rmu.k_skip])):
    if (bool(config[rmu.k_script_steps][rmu.k_reset_man_pages][rmu.k_stop])):
        input("Press a key to proceed")
    reset_man_pages()
    print("Done")
else:
    print("Skipped")

print("\n********** Step 6: reset release_notes **********\n")
if (config[rmu.k_is_official_version]):
    if (interactive):
        rmu.ask_for_step_skip(rmu.k_reset_release_notes)
    if (not bool(config[rmu.k_script_steps][rmu.k_reset_release_notes][rmu.k_skip])):
        if (bool(config[rmu.k_script_steps][rmu.k_reset_release_notes][rmu.k_stop])):
            input("Press a key to proceed")
        reset_release_notes()
        print("Done")
    else:
        print("Skipped")
else:
    print("Must not be done")

print("\n********** commit **********\n")
directories_for_add = []
if ("contrib/devtools/release_management/configs" in config_file):
    directories_for_add.append("contrib/devtools/release_management/configs")
if (not rmu.git_commit(config[rmu.k_repository_root], f"Release {config[rmu.k_version]}{rmu.get_build_suffix(int(config[rmu.k_build_number]))} backport", directories_for_add)):
    print("Commit failed")
    sys.exit(-1)
print("Script execution completed")