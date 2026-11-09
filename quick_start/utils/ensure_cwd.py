import os
RepoName = "p15"

def assert_cwd():
    cwd = os.getcwd()
    if os.path.basename(cwd) != RepoName:
        print(f"Script should be ran from repository root ({RepoName})")
        print(f"Current working directory: {cwd}")
        exit(1)