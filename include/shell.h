/*
 * shell.h - Shell core interface (Person 1).
 *
 * The shell turns a line of text into commands and runs them.
 *
 *   "cat < in.txt | grep hi > out.txt &"
 *        |
 *        v  parse_line()
 *   Pipeline { cmds[0] = cat (input from in.txt)
 *              cmds[1] = grep hi (output to out.txt)
 *              background = 1 }
 *        |
 *        v  execute_pipeline()
 *   built-in commands are called directly, other programs use fork/exec
 *
 * Redirection rule:
 *   - a single BUILT-IN command (echo, ls, cat, ...) with '>' or '>>'
 *     writes into the VIRTUAL disk:   echo hi > notes.txt
 *   - real programs and pipelines use REAL Linux files:
 *     ls | sort > out.txt     wc -l < /etc/passwd
 */
#ifndef SHELL_H
#define SHELL_H

#include "common.h"

#define MAX_PIPE_CMDS 8   /* most commands in one pipeline: a | b | c ... */

/* One command, e.g.  grep "hello world" < in.txt > out.txt */
typedef struct {
    char *argv[MAX_ARGS + 1];  /* words; argv[argc] is always NULL (exec needs it) */
    int   argc;                /* number of words                              */
    char *infile;              /* file after '<', or NULL if none              */
    char *outfile;             /* file after '>' or '>>', or NULL if none      */
    int   append;              /* 1 if '>>' (add to end), 0 if '>' (overwrite) */
} Command;

/* A whole line: one or more commands joined by '|', maybe ending in '&'. */
typedef struct {
    Command cmds[MAX_PIPE_CMDS];
    int     ncmds;             /* 0 means the line was empty      */
    int     background;        /* 1 if the line ended with '&'    */
    char    cmdline[MAX_LINE]; /* copy of the original line (for 'jobs') */
} Pipeline;

/* Splits 'line' into a Pipeline. Handles spaces, "double" and 'single'
 * quotes, '|', '<', '>', '>>' and a trailing '&'.
 * The words are stored in memory from our own allocator (mem.h).
 * Returns MS_OK, or MS_ERR (and prints why) on a syntax error.
 * ALWAYS call free_pipeline() afterwards, even after an error. */
int parse_line(const char *line, Pipeline *out);

/* Frees all memory parse_line() allocated inside 'p'. */
void free_pipeline(Pipeline *p);

/* Runs every command in 'p'.
 * Returns the exit status of the last command (0 = success). */
int execute_pipeline(Pipeline *p);

/* Looks up a built-in command by name, e.g. find_builtin("ls").
 * Returns its function, or NULL if 'name' is not a built-in. */
command_fn find_builtin(const char *name);

/* Built-ins owned by the shell itself */
int cmd_exit(int argc, char **argv);   /* exit                       */
int cmd_echo(int argc, char **argv);   /* echo some words            */
int cmd_help(int argc, char **argv);   /* help (list all built-ins)  */

/* Returns 1 once 'exit' has been run, so main() knows to stop. */
int shell_should_exit(void);

#endif /* SHELL_H */
