#!/bin/sh
# integration.sh - Runs the REAL ./minishell with a script of commands and
# checks its output. This tests all four modules working together,
# including saving the virtual disk and loading it again.

DISK=build/integration.disk
rm -f "$DISK"
fail=0

# check "<expected line>" "<description>"   (exact match of a whole line)
check() {
    if printf '%s\n' "$OUT" | grep -qxF -- "$1"; then
        echo "  ok    $2"
    else
        echo "  FAIL  $2   (expected a line: $1)"
        fail=1
    fi
}

echo "Integration tests:"

# ---- Run 1 -------------------------------------------------------------
OUT=$(./minishell "$DISK" 2>&1 <<'SCRIPT'
mkdir docs
cd docs
write notes.txt hello from minishell
echo second line >> notes.txt
cat notes.txt
pwd
cat notes.txt | wc -l
cat notes.txt | tr a-z A-Z | sort
ls -l
chmod 600 notes.txt
ls -l
sched 2 5 3 8
login alice alice
whoami
pwd
mkdir /docs/hack
cat /docs/notes.txt
nosuchcommand
ls |
exit
SCRIPT
)

check "hello from minishell"                 "write + cat on the virtual disk"
check "second line"                          "echo >> appends to a virtual file"
check "/docs"                                "cd + pwd"
check "2"                                    "built-in piped into a real program (wc)"
check "HELLO FROM MINISHELL"                 "three-stage pipeline"
check "-rw-r--r--  root   root      33  notes.txt" "ls -l shows owner, group, size"
check "-rw-------  root   root      33  notes.txt" "chmod changes permissions"
check "  Average waiting time:    7.00"      "Round Robin simulation"
check "alice"                                "login + whoami"
check "/home/alice"                          "login moves to the home directory"
check "mkdir: /docs/hack: Permission denied" "alice cannot write in root's directory"
check "cat: /docs/notes.txt: Permission denied" "alice cannot read a 600 file"
check "minishell: nosuchcommand: command not found" "unknown command"
check "minishell: syntax error: missing command after '|'" "syntax error"

# ---- Run 2: the disk was saved on exit, so the files are still there ----
OUT=$(printf 'cat /docs/notes.txt\nls /\n' | ./minishell "$DISK" 2>&1)
check "hello from minishell"                 "disk saved on exit and loaded on start"
check "docs/"                                "directories survive a restart"

# ---- Background jobs ----------------------------------------------------
OUT=$(printf 'sleep 5 &\njobs\nkill 1\nsleep 0.2\njobs\n' | ./minishell "$DISK" 2>&1)
check "[1]  Running   sleep 5 &"             "background job appears in jobs"
check "[1]  Terminated   sleep 5 &"          "kill terminates a job"

rm -f "$DISK"
[ "$fail" -eq 0 ] && echo "all integration tests passed"
exit "$fail"
