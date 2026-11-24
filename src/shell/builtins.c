/*
 * builtins.c - The table of built-in commands, plus exit/echo/help (Person 1).
 *
 * To add a new built-in: write   int cmd_x(int argc, char **argv)
 * in your module, declare it in your header, and add one line below.
 */
#include <string.h>
#include "shell.h"
#include "fs.h"
#include "proc.h"
#include "mem.h"
#include "users.h"

typedef struct {
    const char *name;
    command_fn  fn;
    const char *help;
} Builtin;

static const Builtin builtins[] = {
    /* shell */
    { "help",     cmd_help,     "help                      show this list" },
    { "exit",     cmd_exit,     "exit                      save the disk and quit" },
    { "echo",     cmd_echo,     "echo words...             print words" },
    /* virtual file system */
    { "ls",       cmd_ls,       "ls [-l] [path]            list a directory" },
    { "cd",       cmd_cd,       "cd [path]                 change directory (none = home)" },
    { "pwd",      cmd_pwd,      "pwd                       print current directory" },
    { "mkdir",    cmd_mkdir,    "mkdir path...             make directories" },
    { "touch",    cmd_touch,    "touch path...             make empty files" },
    { "cat",      cmd_cat,      "cat [path...]             print files" },
    { "rm",       cmd_rm,       "rm path...                delete files / empty directories" },
    { "write",    cmd_write,    "write [-a] path [text]    write text into a file (-a = append)" },
    /* processes */
    { "jobs",     cmd_jobs,     "jobs                      list jobs" },
    { "ps",       cmd_ps,       "ps                        list processes" },
    { "fg",       cmd_fg,       "fg [job]                  bring a job to the foreground" },
    { "bg",       cmd_bg,       "bg [job]                  resume a stopped job in the background" },
    { "kill",     cmd_kill,     "kill <job>                terminate a job" },
    { "sched",    cmd_sched,    "sched <q> <burst>...      simulate Round Robin with quantum q" },
    /* memory */
    { "memstat",  cmd_memstat,  "memstat                   memory usage and fragmentation" },
    { "memalloc", cmd_memalloc, "memalloc <bytes>          allocate a demo block" },
    { "memfree",  cmd_memfree,  "memfree <handle>          free a demo block" },
    /* users */
    { "login",    cmd_login,    "login <user> [password]   switch user" },
    { "whoami",   cmd_whoami,   "whoami                    print current user" },
    { "chmod",    cmd_chmod,    "chmod <mode> path...      change permissions, e.g. 640" },
};

#define NUM_BUILTINS ((int)(sizeof builtins / sizeof builtins[0]))

static int exit_requested = 0;

command_fn find_builtin(const char *name)
{
    for (int i = 0; i < NUM_BUILTINS; i++)
        if (strcmp(builtins[i].name, name) == 0)
            return builtins[i].fn;
    return NULL;
}

int shell_should_exit(void)
{
    return exit_requested;
}

int cmd_exit(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    exit_requested = 1;
    return MS_OK;
}

int cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        printf("%s%s", argv[i], i < argc - 1 ? " " : "");
    printf("\n");
    return MS_OK;
}

int cmd_help(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    printf("Built-in commands:\n");
    for (int i = 0; i < NUM_BUILTINS; i++)
        printf("  %s\n", builtins[i].help);
    printf("\nAny other command (grep, sort, wc, sleep, ...) runs as a real program.\n"
           "File commands above work on the VIRTUAL disk; use /bin/ls etc. for the real one.\n"
           "Also: cmd1 | cmd2,  cmd > file,  cmd >> file,  cmd < file,  cmd &\n");
    return MS_OK;
}
