"""
A script to list source files in a directory recursively and update their copyright headers.

The script accepts command-line arguments to specify the directory path, extensions of files to include,
subfolders to exclude, date range for filtering modified files, and the copyright holder name.

Usage:
    python update-copyright-headers.py [directory] [--exclude-dir [EXCLUDE_DIR [EXCLUDE_DIR ...]]] [--extensions [EXTENSIONS [EXTENSIONS ...]]]
                                       [--since YYYY-MM-DD] [--until YYYY-MM-DD] [--copyright-holder COPYRIGHT_HOLDER] [--exclude-commit [EXCLUDE_COMMIT [EXCLUDE_COMMIT ...]]]
"""

import argparse
import os
import re
from datetime import datetime

import git

DEFAULT_COPYRIGHT_HOLDER = "The Horizen Foundation"
DEFAULT_EXCLUDED_SUBDIRS = ["leveldb", "snark", "secp256k1", "univalue", "zcash"]
DEFAULT_INCLUDED_EXTENSIONS = [".cpp", ".h", ".hpp"]

# This is a safe date immediately before PR #613, which changed the header from Zen Blockchain Foundation to The Horizen Foundation
DEFAULT_SINCE_DATE = datetime.fromisoformat("2023-09-28")
DEFAULT_UNTIL_DATE = datetime.now()



def parse_arguments():
    """
    Parse command-line arguments and return the parsed arguments.
    """
    parser = argparse.ArgumentParser(description="List files in a directory recursively")
    parser.add_argument("directory", nargs='?', default="src",
                        help="Path to the directory (default: 'src')")
    parser.add_argument("--exclude-dir", nargs='*', default=DEFAULT_EXCLUDED_SUBDIRS,
                        help=f"Folders to exclude from listing, by default {DEFAULT_EXCLUDED_SUBDIRS}")
    parser.add_argument("--extensions", nargs='?', default=DEFAULT_INCLUDED_EXTENSIONS,
                        help=f"Extensions of files to include in the list, by default {DEFAULT_INCLUDED_EXTENSIONS}")
    parser.add_argument("--since", nargs='?', default=DEFAULT_SINCE_DATE, type=lambda s: datetime.strptime(s, '%Y-%m-%d'),
                        help=f"Start date (format: YYYY-MM-DD) for filtering changed files, by default {DEFAULT_SINCE_DATE}")
    parser.add_argument("--until", nargs='?', default=DEFAULT_UNTIL_DATE, type=lambda s: datetime.strptime(s, '%Y-%m-%d'),
                        help="End date (format: YYYY-MM-DD) for filtering changed files, by default current time (now)")
    parser.add_argument("--copyright-holder", default=DEFAULT_COPYRIGHT_HOLDER,
                        help=f"Copyright holder name (default: '{DEFAULT_COPYRIGHT_HOLDER}')")
    parser.add_argument("--exclude-commit", nargs='*', default=[],
                        help="Commits to exclude from the changes made to files, by default automatically detected")
    return parser.parse_args()

def validate_arguments(args):
    """
    Validate the input arguments.
    """
    # Check if the provided path corresponds to a directory
    if not os.path.isdir(args.directory):
        print("Error: Invalid directory path.")
        exit(1)

    # Check if the specified excluded folders exist
    for folder in args.exclude_dir:
        # Check if the folder path is absolute or relative
        path = folder if os.path.isabs(folder) else os.path.join(args.directory, folder)
        if not os.path.exists(path):
            print(f"Error: Folder '{path}' does not exist.")
            exit(1)

    # Check if both --since and --until are provided
    if (args.since and args.until and args.since > args.until):
        print("Error: until_date must be higher than since_date.")
        exit(1)

    # Check if excluded commits are valid and convert them to proper objects
    repo = git.Repo(search_parent_directories=True)

    commit_objects = []
    for commit_hash in args.exclude_commit:
        try:
            commit = repo.commit(commit_hash)
            commit_objects.append(commit)
        except git.BadName:
            print(f"Error: Commit '{commit_hash}' is invalid (not found in the Git history for the specified range).")
            exit(1)

    assert(len(commit_objects) == len(args.exclude_commit))
    args.exclude_commit = commit_objects

def guess_commits_to_be_excluded(since_date, until_date):
    """
    Guess the commits that the user may want to exclude from the check.
    The best use case is to exclude commits that changed the copyright header itself.

    The "guess" is based on the content of the commit message (so, expect it to be not 100% accurate).
    """
    repo = git.Repo(search_parent_directories=True)
    commits = list(repo.iter_commits(since=since_date, until=until_date))
    excluded = [commit for commit in commits if "update-copyright-headers" in commit.message or "copyright" in commit.summary]
    return excluded

def confirm_excluded_commits(excluded_commits):
    """
    Ask user for confirmation about the list of commits to be excluded.
    The user can decide to proceed with the proposed selection,
    ignore the selection or abort.
    """
    print("The following commits have been automatically selected "
          "for exclusion as they may refer to copyright updates:\n")
    for commit in excluded_commits:
        print(f"[{commit.hexsha}] {commit.committed_datetime}")
        print(commit.summary)
        print()

    while True:
        answer = input("Do you want to proceed? [Yes/No/Ignore]: ")

        match answer.lower():
            case "y":
                return excluded_commits
            case "n":
                exit(1)
            case "i":
                return []

def list_files(path, exclude_folders, extensions):
    """
    Recursively traverse a directory and return a list of files with specified extensions.
    """
    file_list = []

    # Get the absolute path of excluded folders
    absolute_path = os.path.abspath(path)
    absolute_excluded_folders = [os.path.normpath(folder) if os.path.isabs(folder) else os.path.normpath(os.path.join(absolute_path, folder)) for folder in exclude_folders]

    for root, dirs, files in os.walk(path):
        # Exclude specified folders by comparing the absolute path
        dirs[:] = [d for d in dirs if os.path.abspath(os.path.join(root, d)) not in absolute_excluded_folders]
        for file in files:
            if file.endswith(tuple(extensions)):
                file_list.append(os.path.join(root, file))

    return file_list

def files_modified_within_date_range(files, since_date, until_date, excluded_commits):
    """
    Check if files in the list were modified within the specified date range.
    Return a list of tuples (file, start_year, end_year) for modified files.
    """
    modified_files = []
    repo = git.Repo(search_parent_directories=True)
    for file in files:
        # Consider only commits that are not explicitly excluded
        commits = list(repo.iter_commits(paths=file, since=since_date, until=until_date))
        commits = [commit for commit in commits if commit.hexsha not in set(map(lambda c: c.hexsha, excluded_commits))]

        if commits:
            start_year = commits[-1].committed_datetime.year
            end_year = commits[0].committed_datetime.year
            modified_files.append((file, start_year, end_year))
    return modified_files

def find_copyright_line(lines, copyright_holder):
    """
    Find the line containing the copyright header or the line where it should be inserted.
    Return the index of the line, since_year, and until_year (if present).
    """
    pattern = fr"// Copyright \(c\) (\d{{{4}}})(?:-(\d{{{4}}}))? {copyright_holder}"

    for i, line in enumerate(lines):
        if not line.startswith("// Copyright (c) "):
            return i, None, None

        match = re.match(pattern, line)
        if match:
            since_year = int(match.group(1))
            until_year = int(match.group(2)) if match.group(2) else since_year
            return i, since_year, until_year

def add_license_notice(lines, index):
    """
    Add license notice if it doesn't exist.
    """
    license_line_1 = "// Distributed under the MIT software license, see the accompanying\n"
    license_line_2 = "// file COPYING or http://www.opensource.org/licenses/mit-license.php.\n"

    # Check if a license notice already exists
    if license_line_1 not in lines[index + 1]:
        lines.insert(index + 1, license_line_1)
        lines.insert(index + 2, license_line_2)

    return lines


def update_copyright(file_path, since_year, until_year, copyright_holder):
    """
    Update the copyright header in the specified file if necessary.
    """

    # This is important for checking consistency
    assert since_year <= until_year

    with open(file_path, 'r') as file:
        lines = file.readlines()

    # Find the line containing the copyright header or the line where it should be inserted
    index, existing_since_year, existing_until_year = find_copyright_line(lines, copyright_holder)

    # Construct the new copyright header string
    if until_year == since_year:
        new_line = f"// Copyright (c) {since_year} {copyright_holder}\n"
    else:
        new_line = f"// Copyright (c) {since_year}-{until_year} {copyright_holder}\n"

    # If the header exists, update it if needed
    if existing_since_year is not None:
        if existing_since_year != since_year or existing_until_year != until_year:
            lines[index] = new_line
        else:
            return
    else:
        # If the header does not exist, append it after the last existing header
        lines.insert(index, new_line)

        # Add the license notice if not already present
        lines = add_license_notice(lines, index)

    # Write the updated content back to the file
    with open(file_path, 'w') as file:
        file.writelines(lines)

if __name__ == "__main__":
    # Parse command-line arguments
    args = parse_arguments()

    # Validate input arguments
    validate_arguments(args)

    # Check excluded commits
    if not args.exclude_commit:
        args.exclude_commit = guess_commits_to_be_excluded(args.since, args.until)
        args.exclude_commit = confirm_excluded_commits(args.exclude_commit)

    # Print the list of excluded commits
    if args.exclude_commit:
        print("Excluding the following commits:")
        for commit in args.exclude_commit:
            print(commit.hexsha)
    else:
        print("No commits to exclude.")

    # Get the list of files in the directory
    print("Gathering files to be checked...")
    project_files = list_files(args.directory, args.exclude_dir, args.extensions)

    # Get files modified, eventually filtering by the specified date range
    print(f"Finding files modified between {args.since} and {args.until}...")
    modified_files = files_modified_within_date_range(project_files, args.since, args.until, args.exclude_commit)

    # Check and eventually updated the copyright headers for each modified file
    print(f"Updating copyright headers for '{args.copyright_holder}'...\n")
    for file, start_year, end_year in modified_files:
        print(f"Updating {file}: {start_year}-{end_year}")
        update_copyright(file, start_year, end_year, args.copyright_holder)

    print("\nDone.")
