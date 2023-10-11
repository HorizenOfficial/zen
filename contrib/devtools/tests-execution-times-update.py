
import requests
import sys
import time

k_python_os_linux = "python_linux"
k_python_os_macos = "python_macos"

k_start_job_id_ubuntu_jammy = 13
k_end_job_id_ubuntu_jammy = 18
k_start_job_id_ubuntu_focal = 22
k_end_job_id_ubuntu_focal = 27
k_start_job_id_macos = 30
k_end_job_id_macos = 42
k_total_jobs = 43

# ---------- Travis API functions ----------

def get_build_history(repo_owner, repo_name, token, pages = 4):
    build_history = []

    base_url = 'https://api.travis-ci.com'
    endpoint = f'/repo/{repo_owner}%2F{repo_name}/builds'
    headers = {'Travis-API-Version': '3', 'Authorization': f'token {token}'}
    url = base_url + endpoint

    for page in range(pages):
        params = {'limit': 25, 'offset': 25 * page}
        maxretry = 2
        for retry in range(maxretry):
            response = requests.get(url, headers=headers, params=params)
            try:
                if response.status_code == 200:
                    build_history.extend(response.json()['builds'])
                    break
            except:
                if retry == maxretry - 1:
                    sys.exit(f"Error retrieving build history: {response.text}")
    
    return build_history

def get_jobs_details(build_id, token):
    base_url = f"https://api.travis-ci.com"
    endpoint = f"/build/{build_id}/jobs"
    headers = {"Travis-API-Version": "3", "Authorization": f"token {token}"}
    url = base_url + endpoint
    
    maxretry = 2
    for retry in range(maxretry):
        response = requests.get(url, headers=headers)
        if response.status_code == 200:
            return response.json()
        else:
            if retry == maxretry - 1:
                sys.exit(f"Error retrieving jobs details: {response.text}")

def get_job_logs(job_id, token):
    base_url = f"https://api.travis-ci.com"
    endpoint = f"/job/{job_id}/log.txt"
    headers = {"Travis-API-Version": "3", "Authorization": f"token {token}"}
    url = base_url + endpoint
    
    maxretry = 2
    for retry in range(maxretry):
        response = requests.get(url, headers=headers)
        if response.status_code == 200:
            return response.text
        else:
            if retry == maxretry - 1:
                sys.exit(f"Error retrieving job logs: {response.text}")

# ---------- Travis API functions ----------

# ---------- Travis python tests per stage ----------

def get_python_stage_type(job_number, total_jobs):
    if (k_start_job_id_ubuntu_jammy <= job_number <= k_end_job_id_ubuntu_jammy or
        k_start_job_id_ubuntu_focal <= job_number <= k_end_job_id_ubuntu_focal):
        return k_python_os_linux
    elif (k_start_job_id_macos <= job_number <= k_end_job_id_macos):
        return k_python_os_macos
    else:
        return ""

# ---------- Travis python tests per stage ----------

# ---------- Auxiliaries ----------

def extract_tests_times(tests_times, os, log):
    while (True):
        start_pos_name = log.find("--- Success: ")
        if (start_pos_name == -1):
            break
        start_pos_name += len("--- Success: ")
        end_pos_name = log.find(" - elapsed time: ", start_pos_name, start_pos_name + 100)
        test_name = log[start_pos_name:end_pos_name]
        test_name = test_name.replace("**", "rt")
        start_pos_time = end_pos_name + len(" - elapsed time: ")
        end_pos_time = log.find(" ---", start_pos_time, start_pos_time + 10)
        test_time = int(log[start_pos_time:end_pos_time])
        if (not test_name in tests_times):
            tests_times[test_name] = {}
        if (not os in tests_times[test_name]):
            tests_times[test_name][os] = []
        tests_times[test_name][os].append(test_time)
        log = log[end_pos_time:-1]

def compute_average_tests_times(tests_times):
    for test_name in tests_times:
        for os in tests_times[test_name]:
            times_list = tests_times[test_name][os]
            tests_times[test_name][os] = int(sum(times_list) / len(times_list))

def update_rpc_tests_sh_file(file_path, tests_times):
    with open(file_path, "r") as file:
        file_data_in = file.read()
    file_data_in = file_data_in.split('\n')

    file_data_out = ""

    line_start_opt_1 = "  '"
    line_start_opt_2 = "  testScripts+=('"
    line_end_opt_1 = ""
    line_end_opt_2 = ")"
    for index in range(len(file_data_in)):
        line_start = ""
        just_copy = False
        if (".py" in file_data_in[index] and "'," in file_data_in[index]):
            if (file_data_in[index].startswith(line_start_opt_1)):
                line_start = line_start_opt_1
                line_end = line_end_opt_1
            elif (file_data_in[index].startswith(line_start_opt_2)):
                line_start = line_start_opt_2
                line_end = line_end_opt_2
            else:
                just_copy = True
            if (not just_copy):
                start_pos_name = file_data_in[index].find(line_start)
                if (start_pos_name == -1):
                    continue
                start_pos_name += len(line_start)
                end_pos_name = file_data_in[index].find("',") # not using ".py" because of cases like 'txn_doublespend.py --mineblock',23,62
                if (end_pos_name == -1):
                    continue
                test_name = file_data_in[index][start_pos_name:end_pos_name]
                if (test_name in tests_times):
                    linux_time = tests_times[test_name][k_python_os_linux] if k_python_os_linux in tests_times[test_name] else 0
                    macos_time = tests_times[test_name][k_python_os_macos] if k_python_os_macos in tests_times[test_name] else 0
                    file_data_out += f"{line_start}{test_name}',{linux_time},{macos_time}{line_end}\n"
                else:
                    just_copy = True
        else:
            just_copy = True

        if (just_copy):
            file_data_out += file_data_in[index]
            if (index < len(file_data_in) - 1):
                file_data_out += "\n"
    
    with open(file_path, "w") as file:
            file.write(file_data_out)

def print_tests_times(tests_times):
    for test_name in tests_times:
        linux_time = tests_times[test_name][k_python_os_linux] if k_python_os_linux in tests_times[test_name] else 0
        macos_time = tests_times[test_name][k_python_os_macos] if k_python_os_macos in tests_times[test_name] else 0
        print(f"'{test_name}',{linux_time},{macos_time}")

def check_missing(tests_times):
    for test_name in tests_times:
        for os in [k_python_os_linux, k_python_os_macos]:
            if (not os in tests_times[test_name]):
                print(f"Missing {os} for {test_name}")

# ---------- Auxiliaries ----------

# Script main

start_time = time.time()

repository_owner = 'HorizenOfficial'
repository_name = 'zen'
travis_token = 'SetHereProperTokenValue'
builds_quantity = 10
rpc_tests_sh_file_path = "./qa/pull-tester/rpc-tests.sh"

for arg in sys.argv:
    if (arg.startswith("travis_token=")):
        travis_token = arg.replace("travis_token=", "")
    if (arg.startswith("builds_quantity=")):
        builds_quantity = int(arg.replace("builds_quantity=", ""))
    if (arg.startswith("rpc_tests_sh_file_path=")):
        rpc_tests_sh_file_path = arg.replace("rpc_tests_sh_file_path=", "")

print("Running with following params:")
print(f"repository_owner (constant): {repository_owner}")
print(f"repository_name (constant): {repository_name}")
print(f"travis_token (configurable): {travis_token}")
print(f"builds_quantity (configurable): {builds_quantity}")
print(f"rpc_tests_sh_file_path (configurable): {rpc_tests_sh_file_path}")

assert(travis_token != 'SetHereProperTokenValue')

tests_times = {}

print("Getting build history")
builds = get_build_history(repository_owner, repository_name, travis_token, int(builds_quantity / 25 + 1)) # retrieving last (builds_quantity / 25 + 1) * 25 builds
builds_accounted = 0
for build in builds:
    if (builds_accounted >= builds_quantity):
        break
    build_id = build['id']
    build_number = build['number']
    build_state = build['state']
    if (build_state == "passed"):
        print(f"Processing a passed build ({build_number})")
        builds_accounted += 1
        jobs = build['jobs']
        if (len(jobs) == k_total_jobs):
            print("Getting jobs details")
            jobs_details = get_jobs_details(build_id, travis_token)
            for job_details in jobs_details["jobs"]:
                job_number = (int)(job_details['number'].split(".")[-1])
                python_stage_type = get_python_stage_type(job_number, len(jobs))
                if (python_stage_type in [k_python_os_linux, k_python_os_macos]):
                    print(f"Getting job logs ({job_number})")
                    job_logs = get_job_logs(job_details["id"], travis_token)
                    extract_tests_times(tests_times, python_stage_type, job_logs)
        else:
            sys.exit("Unsupported .travis.yml file (os cannot be identified)")

print(f"Averaging over {builds_accounted} accounted builds")
compute_average_tests_times(tests_times)
print_tests_times(tests_times)
check_missing(tests_times)
update_rpc_tests_sh_file(rpc_tests_sh_file_path, tests_times)
print(f"Script execution time: {time.time() - start_time}")
temp = 0
