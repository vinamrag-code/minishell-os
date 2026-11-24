# MiniShell OS

A mini Linux-style shell written in C, with its own **virtual file system**,
**process manager**, **memory allocator**, and **users & permissions**.

Built as a 4-person Operating Systems project. It uses only standard C and
POSIX APIs (no external libraries) and runs on Linux or WSL.

**Status:** all four modules work. `make test` runs 214 unit checks and
18 integration checks, and all of them pass.

---

## Quick demo

```
root@minishell:/$ mkdir docs
root@minishell:/$ cd docs
root@minishell:/docs$ write notes.txt hello from minishell
root@minishell:/docs$ echo second line >> notes.txt
root@minishell:/docs$ cat notes.txt | tr a-z A-Z
HELLO FROM MINISHELL
SECOND LINE
root@minishell:/docs$ ls -l
-rw-r--r--  root   root      33  notes.txt
root@minishell:/docs$ sleep 30
^Z
[1]  Stopped   sleep 30
root@minishell:/docs$ bg 1
[1]  sleep 30 &
root@minishell:/docs$ sched 2 5 3 8
Round Robin, quantum = 2
  t=  0-2    P1   runs (3 left)
  ...
  Average waiting time:    7.00
root@minishell:/docs$ login alice alice
Logged in as alice
alice@minishell:/home/alice$ memstat
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
make                      # builds ./minishell
make test                 # unit tests + integration tests
./minishell               # start the shell (type 'help', 'exit' to quit)
./minishell other.disk    # use a different virtual disk file
make clean                # delete build files
```

The virtual disk is saved to `minishell.disk` when you exit, and loaded
again next time. To start with a fresh disk, delete that file.

---

## Commands

Type `help` inside the shell to see this list.

| Area | Command | What it does |
|------|---------|--------------|
| Shell | `help`, `exit`, `echo words...` | |
| Files (virtual disk) | `ls [-l] [path]` | list a directory |
| | `cd [path]` | change directory (no path = your home) |
| | `pwd` | print current directory |
| | `mkdir path...` / `touch path...` | make directories / empty files |
| | `cat [path...]` | print files (no path = read stdin) |
| | `rm path...` | delete files or empty directories |
| | `write [-a] path [text...]` | write text into a file (`-a` = append; no text = read stdin) |
| Processes | `jobs`, `ps` | list jobs / processes |
| | `fg [job]`, `bg [job]` | resume a stopped job in front / in the background |
| | `kill <job>` | terminate a job |
| | `sched <quantum> <burst>...` | Round Robin simulation, e.g. `sched 2 5 3 8` |
| | Ctrl+C / Ctrl+Z | interrupt / stop the program in front |
| Memory | `memstat` | pool usage, fragmentation, block map |
| | `memalloc <bytes>`, `memfree <handle>` | allocate/free demo blocks to create fragmentation |
| Users | `login <user> [password]` | switch user |
| | `whoami` | print current user |
| | `chmod <mode> path...` | change permissions, e.g. `chmod 640 notes.txt` |

Any other command (`grep`, `sort`, `wc`, `sleep`, `tr` ...) runs as a **real
Linux program**. Our `ls`, `cat`, `rm` etc. work on the **virtual** disk;
use `/bin/ls` for the real one.

### Pipes and redirection

```
cmd1 | cmd2 | cmd3      pipes (up to 8 commands)
cmd > file              write output (overwrite)
cmd >> file             write output (append)
cmd < file              read input
cmd &                   run in the background
"..." or '...'          keep spaces inside one argument
```

**Which disk does a redirection use?**
- A single **built-in** command writes into the **virtual** disk:
  `echo hi > notes.txt`, `ls -l > list.txt`
- **Real programs and pipelines** use **real Linux files**:
  `cat notes.txt | sort > /tmp/sorted.txt`, `wc -l < /etc/passwd`

### Users

| User | Password | Group | Home |
|------|----------|-------|------|
| root | root | root | `/` (may do anything) |
| alice | alice | users | `/home/alice` |
| bob | bob | users | `/home/bob` |
| carol | carol | staff | `/home/carol` |

The shell starts as `root`. `/tmp` is writable by everyone.
(Passwords are plain text because this is a simulation. A real system never does that.)

---

## Modules and owners

| # | Module | Owner | Files | What it covers |
|---|--------|-------|-------|----------------|
| 1 | Shell core | Person 1 | `src/shell/`, `src/main.c` | Parsing (quotes, `\|`, `<`, `>`, `>>`, `&`), `fork`/`exec`/`wait`, pipes, redirection, built-in table |
| 2 | Virtual file system | Person 2 | `src/fs/vfs.c` | 512 KB in-memory disk, inodes, directories, free-block bitmap, save/load |
| 3 | Process management | Person 3 | `src/proc/` | Job table, process groups, Ctrl+C/Ctrl+Z, `fg`/`bg`/`kill`, Round Robin |
| 4 | Memory, permissions, testing | Person 4 | `src/mem/`, `tests/` | First-fit allocator with coalescing, `memstat`, users, `rwx` checks, test suite |

The modules only talk to each other through the header files in `include/`.
**Start by reading `include/common.h` and your own module's header.** Each
one explains its design at the top.

How the modules connect:
- The **parser** stores every word you type in the **custom allocator's** pool, so `memstat` shows real usage.
- The **VFS** asks `perm_check()` (users) before every read and write.
- The **executor** hands every pipeline to the **job table**, which waits for it and handles Ctrl+Z.

---

## Folder structure

```
minishell-os/
├── Makefile
├── README.md
├── include/            # module interfaces (the "contracts")
│   ├── common.h        # shared constants, return codes, command signature
│   ├── shell.h         # Person 1
│   ├── fs.h            # Person 2
│   ├── proc.h          # Person 3
│   ├── mem.h           # Person 4 (allocator)
│   └── users.h         # Person 4 (users & permissions)
├── src/
│   ├── main.c          # starts the modules, runs the prompt loop
│   ├── shell/          # parser.c, executor.c, builtins.c
│   ├── fs/             # vfs.c
│   ├── proc/           # jobs.c, signals.c, scheduler.c
│   └── mem/            # allocator.c, users.c
└── tests/
    ├── test.h          # tiny test framework: CHECK(condition)
    ├── test_main.c     # runs all unit tests
    ├── test_*.c        # one file per module
    └── integration.sh  # runs the real shell with a script and checks the output
```

---

## Testing

`make test` runs two layers:

1. **Unit tests** (`build/run_tests`): each module on its own. Examples:
   parser edge cases, first-fit hole reuse, coalescing, the free-block
   bitmap after `rm`, save/load, owner/group/other permissions, the Round
   Robin worked example, and real child processes being stopped and resumed.
2. **Integration tests** (`tests/integration.sh`): run `./minishell` with
   a script of commands and check the output, including a restart to
   prove the disk is saved.

To add a test, put `CHECK(condition);` in your module's `tests/test_*.c`.

To catch memory bugs, run the tests with AddressSanitizer:

```bash
make clean
make test CFLAGS="-Wall -Wextra -std=c11 -g -Iinclude -D_POSIX_C_SOURCE=200809L -fsanitize=address,undefined"
```

---

## Limits (by design, to keep the code simple)

| Thing | Limit |
|-------|-------|
| Virtual disk | 1024 blocks × 512 bytes = 512 KB |
| Files + directories | 128 inodes |
| One file | 12 direct blocks = 6 KB |
| File name | 27 characters |
| Memory pool | 64 KB |
| Pipeline | 8 commands |
| Jobs | 32 |
| Round Robin | 16 simulated processes, all arriving at time 0 |

---

## Team workflow

1. **Pull before you start:** `git pull`
2. **Work on your own branch:** `git checkout -b fs-indirect-blocks`
3. **Keep each commit to one logical change**, with a conventional message:
   - `feat:` a new feature (for example, `feat(fs): add mv command`)
   - `fix:` a bug fix
   - `test:` adding or changing tests
   - `docs:` documentation only (README, comments)
   - `chore:` setup, build, or tidying
4. **Build and test before every push:** `make test` must pass.
5. **Open a Pull Request** and ask one teammate to review it before merging.

### Coding style

- Clear code beats clever code. Write it for a beginner to read.
- Put a comment at the top of every function saying what it does.
- Functions return `MS_OK` on success and `MS_ERR` on failure (from `common.h`).
- Build with `-Wall -Wextra` and treat every warning as a bug.

---

## Key OS concepts in this project

| Concept | Plain explanation | Real-life analogy | Where in the code |
|---------|-------------------|-------------------|-------------------|
| **fork** | Makes a copy of the running program | Photocopying a form so someone else can fill in the copy | `executor.c` |
| **exec** | Replaces the running program with another one | Keeping the same desk but swapping in a new worker | `executor.c` |
| **pipe** | Connects one program's output to another program's input | A water pipe between two tanks | `executor.c` |
| **signal** | A short message the OS sends to a process | A tap on the shoulder ("stop!", "pause!") | `signals.c` |
| **process group** | All processes of one job, so a signal reaches all of them | Calling a whole team by its name | `executor.c`, `jobs.c` |
| **inode** | A record describing a file: its size, owner, and where its data is stored | A library catalogue card that says which shelf a book is on | `vfs.c` |
| **bitmap** | One bit per disk block: 1 = used, 0 = free | A cinema seat map showing which seats are taken | `vfs.c` |
| **first-fit** | Use the first free block that is big enough | Parking in the first empty space you see | `allocator.c` |
| **fragmentation** | Free memory split into pieces too small to use | Empty seats scattered around, so a group of 4 can't sit together | `allocator.c` |
| **Round Robin** | Each process runs for a short time slice, then goes to the back of the queue | Passing a microphone around a circle, one turn each | `scheduler.c` |

---

## Ideas for extensions

- `mv` and `cp` for the virtual disk
- Indirect blocks, so files can be bigger than 6 KB
- Best-fit or worst-fit allocation, with a command to compare fragmentation
- Arrival times and priorities in the scheduler simulation
- Command history (up arrow)
