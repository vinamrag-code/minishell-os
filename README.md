# MiniShell OS

A mini Linux-style shell written in C, with its own **virtual file system**,
**process manager**, **memory allocator**, and **users & permissions**.

Built as a 4-person Operating Systems project. It uses only standard C and
POSIX APIs (no external libraries) and runs on Linux or WSL.

> **Status:** Step 1 (project skeleton and module interfaces) is in progress.
> The commands listed below are the plan. They are not all working yet.

---

## What it does

```
minishell:/home/alice$ mkdir notes
minishell:/home/alice$ write notes/todo.txt "finish the scheduler"
minishell:/home/alice$ cat notes/todo.txt
finish the scheduler
minishell:/home/alice$ ls -l | grep txt
minishell:/home/alice$ sleep 30 &
[1] 4821
minishell:/home/alice$ jobs
[1]  Running   sleep 30
minishell:/home/alice$ memstat
```

---

## Modules and owners

| # | Module | Owner | Folder | What it covers |
|---|--------|-------|--------|----------------|
| 1 | Shell core | Person 1 | `src/shell/` | Parsing (commands, arguments, quotes), `fork`/`exec`/`wait`, pipes `\|`, redirection `>` `<`, background jobs `&` |
| 2 | Virtual file system | Person 2 | `src/fs/` | An in-memory fake disk (can be saved to one file), inodes, directories, a free-block bitmap |
| 3 | Process management | Person 3 | `src/proc/` | Job table, job states, signals (Ctrl+C, Ctrl+Z), Round Robin scheduler simulation |
| 4 | Memory, permissions, testing | Person 4 | `src/mem/`, `tests/` | First-fit `malloc`/`free` over a fixed pool, fragmentation stats, users, `rwx` permissions, test suite |

The modules only talk to each other through the header files in `include/`.
If you need something from another module, agree on a change to its header
with that module's owner first.

---

## Planned commands

| Area | Commands |
|------|----------|
| File system | `ls`, `cd`, `pwd`, `mkdir`, `touch`, `cat`, `rm`, `write` / `echo > file` |
| Processes | `ps`, `jobs`, `fg`, `bg`, `kill`, `sched` (Round Robin simulation) |
| Memory | `memstat` (memory usage and fragmentation) |
| Users | `login`, `whoami`, `chmod` |
| Shell | `exit`, plus any real Linux program (`ls`, `grep`, `sleep`, ...) through `fork`/`exec` |

---

## Folder structure

```
minishell-os/
├── Makefile
├── README.md
├── include/          # module interfaces (the "contracts")
│   ├── common.h      # shared constants, return codes, command signature
│   ├── shell.h       # Person 1
│   ├── fs.h          # Person 2
│   ├── proc.h        # Person 3
│   ├── mem.h         # Person 4 (allocator)
│   └── users.h       # Person 4 (users & permissions)
├── src/
│   ├── main.c        # program entry point and prompt loop
│   ├── shell/        # parser.c, executor.c, builtins.c
│   ├── fs/           # vfs.c
│   ├── proc/         # jobs.c, signals.c, scheduler.c
│   └── mem/          # allocator.c, users.c
└── tests/            # test.h, test_main.c, one test file per module
```

---

## Getting started (Linux / WSL)

On Windows, open PowerShell and type `wsl` to get a Linux terminal. Then run:

```bash
# 1. Install the compiler and make (needed only once)
sudo apt update && sudo apt install -y build-essential git

# 2. Get the code
git clone https://github.com/vinamrag-code/minishell-os.git
cd minishell-os

# 3. Build, test, run
make          # builds ./minishell
make test     # builds and runs all tests
./minishell   # starts the shell (type `exit` to quit)
make clean    # deletes build files
```

---

## Team workflow

1. **Pull before you start:** `git pull`
2. **Work on your own branch:** `git checkout -b fs-inodes`
3. **Keep each commit to one logical change**, with a conventional message:
   - `feat:` a new feature (for example, `feat(fs): add mkdir and ls`)
   - `fix:` a bug fix
   - `test:` adding or changing tests
   - `docs:` documentation only (README, comments)
   - `chore:` setup, build, or tidying
4. **Build and test before every push:** `make && make test` must pass.
5. **Open a Pull Request** and ask one teammate to review it before merging.

### Coding style

- Clear code beats clever code. Write it for a beginner to read.
- Put a comment at the top of every function saying what it does.
- Functions return `MS_OK` on success and `MS_ERR` on failure (from `common.h`).
- Build with `-Wall -Wextra` and treat every warning as a bug.

---

## Key OS concepts in this project

| Concept | Plain explanation | Real-life analogy |
|---------|-------------------|-------------------|
| **fork** | Makes a copy of the running program | Photocopying a form so someone else can fill in the copy |
| **exec** | Replaces the running program with another one | Keeping the same desk but swapping in a new worker |
| **pipe** | Connects one program's output to another program's input | A water pipe between two tanks |
| **signal** | A short message the OS sends to a process | A tap on the shoulder ("stop!", "pause!") |
| **inode** | A record describing a file: its size, owner, and where its data is stored | A library catalogue card that says which shelf a book is on |
| **bitmap** | One bit per disk block: 1 = used, 0 = free | A cinema seat map showing which seats are taken |
| **fragmentation** | Free memory split into pieces too small to use | Empty seats scattered around, so a group of 4 can't sit together |
| **Round Robin** | Each process runs for a short time slice, then goes to the back of the queue | Passing a microphone around a circle, one turn each |
