/*
 * test_shell.c - Tests for the shell core (Person 1):
 * parsing, running programs, pipes, redirection.
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test.h"
#include "shell.h"
#include "fs.h"
#include "mem.h"
#include "users.h"

/* Parses and runs one line. Returns its exit status (-1 = syntax error). */
static int run(const char *line)
{
    Pipeline p;
    int status = -1;
    if (parse_line(line, &p) == MS_OK)
        status = execute_pipeline(&p);
    free_pipeline(&p);
    return status;
}

/* Parses a line that must FAIL with a syntax error. */
static int parse_fails(const char *line)
{
    Pipeline p;
    quiet_stderr(1);
    int rc = parse_line(line, &p);
    quiet_stderr(0);
    free_pipeline(&p);
    return rc == MS_ERR;
}

static void test_parse(void)
{
    Pipeline p;

    /* simple command */
    CHECK(parse_line("ls -l /home\n", &p) == MS_OK);
    CHECK(p.ncmds == 1);
    CHECK(p.cmds[0].argc == 3);
    CHECK(strcmp(p.cmds[0].argv[0], "ls") == 0);
    CHECK(strcmp(p.cmds[0].argv[2], "/home") == 0);
    CHECK(p.cmds[0].argv[3] == NULL);          /* exec needs the NULL */
    CHECK(p.background == 0);
    CHECK(strcmp(p.cmdline, "ls -l /home") == 0);
    free_pipeline(&p);

    /* quotes keep spaces; quotes next to text join into one word */
    CHECK(parse_line("write a.txt \"hello   world\" 'it''s' he\"ll\"o", &p) == MS_OK);
    CHECK(p.cmds[0].argc == 5);
    CHECK(strcmp(p.cmds[0].argv[2], "hello   world") == 0);
    CHECK(strcmp(p.cmds[0].argv[3], "its") == 0);
    CHECK(strcmp(p.cmds[0].argv[4], "hello") == 0);
    free_pipeline(&p);

    /* pipeline + redirection + background */
    CHECK(parse_line("cat < in.txt | grep hi | sort >> out.txt &", &p) == MS_OK);
    CHECK(p.ncmds == 3);
    CHECK(strcmp(p.cmds[0].infile, "in.txt") == 0);
    CHECK(strcmp(p.cmds[1].argv[1], "hi") == 0);
    CHECK(strcmp(p.cmds[2].outfile, "out.txt") == 0);
    CHECK(p.cmds[2].append == 1);
    CHECK(p.background == 1);
    free_pipeline(&p);

    /* operators do not need spaces around them */
    CHECK(parse_line("echo hi>f.txt", &p) == MS_OK);
    CHECK(p.cmds[0].argc == 2);
    CHECK(strcmp(p.cmds[0].outfile, "f.txt") == 0);
    CHECK(p.cmds[0].append == 0);
    free_pipeline(&p);

    /* empty line is fine, nothing to run */
    CHECK(parse_line("   \n", &p) == MS_OK);
    CHECK(p.ncmds == 0);
    free_pipeline(&p);

    /* syntax errors */
    CHECK(parse_fails("ls |"));
    CHECK(parse_fails("| ls"));
    CHECK(parse_fails("ls | | wc"));
    CHECK(parse_fails("ls >"));
    CHECK(parse_fails("cat < | wc"));
    CHECK(parse_fails("echo \"unterminated"));
    CHECK(parse_fails("sleep 1 & ls"));
    CHECK(parse_fails("&"));
    CHECK(parse_fails("> out.txt"));

    /* the parser must give back every byte it took from our allocator */
    MemStats s;
    mem_get_stats(&s);
    CHECK(s.used_blocks == 0);
}

static void test_execute(void)
{
    char real1[] = "/tmp/minishell_test_XXXXXX";
    char real2[] = "/tmp/minishell_test_XXXXXX";
    close(mkstemp(real1));
    close(mkstemp(real2));
    char line[MAX_LINE], buf[256];

    /* exit status of real programs */
    CHECK(run("true") == 0);
    CHECK(run("false") == 1);
    quiet_stderr(1);
    CHECK(run("no_such_command_xyz") == 127);
    quiet_stderr(0);

    /* pipe + real output redirection */
    snprintf(line, sizeof line, "printf 'b\\na\\n' | sort > %s", real1);
    CHECK(run(line) == 0);
    CHECK(read_real_file(real1, buf, sizeof buf) == 4);
    CHECK(strcmp(buf, "a\nb\n") == 0);

    /* append */
    snprintf(line, sizeof line, "printf 'c\\n' >> %s", real1);
    CHECK(run(line) == 0);
    read_real_file(real1, buf, sizeof buf);
    CHECK(strcmp(buf, "a\nb\nc\n") == 0);

    /* input redirection */
    snprintf(line, sizeof line, "wc -l < %s > %s", real1, real2);
    CHECK(run(line) == 0);
    read_real_file(real2, buf, sizeof buf);
    CHECK(atoi(buf) == 3);

    /* a single built-in with '>' writes to the VIRTUAL disk */
    CHECK(run("echo hello world > /greet.txt") == 0);
    CHECK(run("echo again >> /greet.txt") == 0);
    int n = vfs_read("/greet.txt", buf, sizeof buf - 1);
    CHECK(n == 18);
    if (n >= 0)
        buf[n] = '\0';
    CHECK(strcmp(buf, "hello world\nagain\n") == 0);

    /* a built-in inside a pipeline feeds a real program */
    snprintf(line, sizeof line, "cat /greet.txt | tr a-z A-Z > %s", real2);
    CHECK(run(line) == 0);
    read_real_file(real2, buf, sizeof buf);
    CHECK(strcmp(buf, "HELLO WORLD\nAGAIN\n") == 0);

    /* three-stage pipeline */
    snprintf(line, sizeof line, "printf 'x\\ny\\nx\\n' | sort | uniq > %s", real2);
    CHECK(run(line) == 0);
    read_real_file(real2, buf, sizeof buf);
    CHECK(strcmp(buf, "x\ny\n") == 0);

    /* built-ins run inside the shell really change its state */
    CHECK(run("cd /tmp") == 0);
    char cwd[MAX_PATH];
    vfs_getcwd(cwd, sizeof cwd);
    CHECK(strcmp(cwd, "/tmp") == 0);
    CHECK(run("cd /") == 0);

    unlink(real1);
    unlink(real2);
}

void test_shell(void)
{
    mem_init();
    users_init();
    vfs_init();
    test_parse();
    test_execute();
}
