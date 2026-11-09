#!/usr/bin/env python3
"""ci_wait.py SHA [MINUTES]: wait for the GitHub Actions runs on main for commit SHA (gh in WSL; read-only) and print
their conclusions. Exit 0 when every run on it succeeded, 1 when one failed, 2 on a timeout (default 30 minutes)."""
import json
import subprocess
import sys
import time

sha = sys.argv[1]
deadline = time.time() + 60 * float(sys.argv[2] if len(sys.argv) > 2 else 30)
NW = subprocess.CREATE_NO_WINDOW
full = subprocess.run(["git", "-C", "C:/Users/julie/gt4-lane-F", "rev-parse", sha], capture_output=True, text=True,
                      creationflags=NW).stdout.strip() or sha
while True:
    r = subprocess.run(["wsl.exe", "-e", "gh", "run", "list", "-R", "Ganitzsh/gt4-native", "-b", "main", "-L", "30", "--json",
                        "headSha,status,conclusion,databaseId,workflowName"], capture_output=True, text=True, creationflags=NW)
    runs = [x for x in json.loads(r.stdout or "[]") if x["headSha"].startswith(full) or full.startswith(x["headSha"])]
    if runs and all(x["status"] == "completed" for x in runs):
        for x in runs:
            print(f"{x['workflowName']} run {x['databaseId']}: {x['conclusion']}")
        sys.exit(0 if all(x["conclusion"] == "success" for x in runs) else 1)
    if time.time() > deadline:
        print("timed out:", [(x["workflowName"], x["status"]) for x in runs] or "no run for that commit yet")
        sys.exit(2)
    time.sleep(60)
