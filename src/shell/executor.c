/*
 * executor.c - Runs a Pipeline (Person 1).
 *
 * Two cases:
 *
 * 1. ONE built-in command, not in the background (cd, ls, echo ...):
 *    called directly inside the shell, so 'cd' and 'login' really change
 *    the shell. A '>' sends its output into the VIRTUAL disk.
 *
 * 2. Everything else: for each command we fork() a child process.
 *    The child connects its stdin/stdout to pipes or real files, then
 *    either runs the built-in or exec()s the real program.
 *    All children of one line share one process group = one job,
 *    so Ctrl+C / Ctrl+Z reach all of them together.
 *
 *    ls | grep a | wc -l
 *
 *    [ls] --pipe--> [grep a] --pipe--> [wc -l] --> terminal
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shell.h"
#include "fs.h"
#include "proc.h"

_Static_assert(MAX_PIPE_CMDS <= JOB_MAX_PROCS,
               "a job must be able to hold a whole pipeline");

/* Built-ins return MS_OK / MS_ERR; programs return 0 / non-zero. */
static int to_status(int rc)
{
    return rc == MS_OK ? 0 : 1;
}

/* Case 1: run a built-in inside the shell process. */
static int run_builtin_in_shell(Command *c, command_fn fn)
{
    if (c->infile != NULL) {
        fprintf(stderr, "minishell: %s: '<' only works with real programs\n",
                c->argv[0]);
        return 1;
    }
    if (c->outfile == NULL)
        return to_status(fn(c->argc, c->argv));

    /* Output goes to the virtual disk. Trick: point stdout at a temporary
     * real file, run the command, then copy what it printed into the VFS. */
    FILE *tmp = tmpfile();
    if (tmp == NULL) {
        perror("minishell: tmpfile");
        return 1;
    }
    fflush(stdout);
    int saved_stdout = dup(STDOUT_FILENO);
    dup2(fileno(tmp), STDOUT_FILENO);

    int rc = fn(c->argc, c->argv);

    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);   /* put the terminal back */
    close(saved_stdout);

    static char buf[FS_MAX_FILE_SIZE + 1];   /* +1 so "too big" is detected */
    rewind(tmp);
    size_t n = fread(buf, 1, sizeof buf, tmp);
    fclose(tmp);

    if (vfs_write(c->outfile, buf, (int)n, c->append) != MS_OK) {
        fprintf(stderr, "minishell: %s: %s\n", c->outfile, vfs_last_error());
        return 1;
    }
    return to_status(rc);
}

/* Opens a real file and puts it on 'target_fd' (0 = stdin, 1 = stdout).
 * Only called in the child, so on error it exits. */
static void redirect_or_die(const char *path, int flags, int target_fd)
{
    int fd = open(path, flags, 0644);
    if (fd < 0) {
        fprintf(stderr, "minishell: %s: %s\n", path, strerror(errno));
        _exit(1);
    }
    dup2(fd, target_fd);
    close(fd);
}

/* Case 2: everything the child process does after fork(). Never returns. */
_Noreturn static void run_child(Command *c, int in_fd, int out_fd, int extra_fd,
                                pid_t pgid, int foreground)
{
    if (extra_fd >= 0)
        close(extra_fd);          /* the read end of our own output pipe */

    /* Join the job's process group (pgid 0 = start a new group with our PID). */
    setpgid(0, pgid);
    if (foreground && proc_interactive())
        tcsetpgrp(STDIN_FILENO, getpgrp());

    signals_reset_child();        /* Ctrl+C / Ctrl+Z work normally again */

    /* Connect pipes. */
    if (in_fd != STDIN_FILENO) {
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    }
    if (out_fd != STDOUT_FILENO) {
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }

    /* Real-file redirection (these win over pipes). */
    if (c->infile)
        redirect_or_die(c->infile, O_RDONLY, STDIN_FILENO);
    if (c->outfile)
        redirect_or_die(c->outfile,
                        O_WRONLY | O_CREAT | (c->append ? O_APPEND : O_TRUNC),
                        STDOUT_FILENO);

    /* A built-in inside a pipeline, e.g.  cat notes.txt | wc -l */
    command_fn fn = find_builtin(c->argv[0]);
    if (fn != NULL) {
        int rc = fn(c->argc, c->argv);
        fflush(stdout);
        fflush(stderr);
        _exit(to_status(rc));
    }

    /* A real program. execvp only returns if it failed. */
    execvp(c->argv[0], c->argv);
    fprintf(stderr, "minishell: %s: command not found\n", c->argv[0]);
    _exit(127);
}

int execute_pipeline(Pipeline *p)
{
    if (p->ncmds == 0)
        return 0;

    if (p->ncmds == 1 && !p->background) {
        command_fn fn = find_builtin(p->cmds[0].argv[0]);
        if (fn != NULL)
            return run_builtin_in_shell(&p->cmds[0], fn);
    }

    pid_t pids[MAX_PIPE_CMDS];
    int started = 0;
    pid_t pgid = 0;                  /* set to the first child's PID */
    int in_fd = STDIN_FILENO;        /* where the next command reads from */
    int foreground = !p->background;

    for (int i = 0; i < p->ncmds; i++) {
        int fds[2] = { -1, -1 };     /* fds[0] = read end, fds[1] = write end */
        int out_fd = STDOUT_FILENO;

        if (i < p->ncmds - 1) {      /* not the last command: make a pipe */
            if (pipe(fds) < 0) {
                perror("minishell: pipe");
                break;
            }
            out_fd = fds[1];
        }

        fflush(stdout);              /* or the child would print our buffer too */
        fflush(stderr);

        pid_t pid = fork();
        if (pid < 0) {
            perror("minishell: fork");
            if (fds[0] >= 0) {
                close(fds[0]);
                close(fds[1]);
            }
            break;
        }
        if (pid == 0)
            run_child(&p->cmds[i], in_fd, out_fd, fds[0], pgid, foreground);

        /* Parent (the shell). Also set the group here, so it is
         * correct no matter whether parent or child runs first. */
        if (pgid == 0)
            pgid = pid;
        setpgid(pid, pgid);
        pids[started++] = pid;

        /* The shell must close its copies of the pipe ends, otherwise
         * the reader never sees "end of input". */
        if (in_fd != STDIN_FILENO)
            close(in_fd);
        if (out_fd != STDOUT_FILENO)
            close(out_fd);
        in_fd = fds[0];
    }
    if (in_fd != STDIN_FILENO)
        close(in_fd);                /* only if the loop stopped early */

    if (started == 0)
        return 1;

    int id = jobs_add(pgid, pids, started, p->cmdline, JOB_RUNNING);
    if (id == MS_ERR) {
        fprintf(stderr, "minishell: too many jobs, stopping this one\n");
        kill(-pgid, SIGKILL);
        for (int i = 0; i < started; i++)
            waitpid(pids[i], NULL, 0);
        return 1;
    }

    if (p->background) {
        printf("[%d] %d\n", id, (int)pgid);
        return 0;
    }

    int status = wait_foreground(id, 0);
    Job *j = jobs_find(id);
    if (j != NULL && j->state == JOB_STOPPED)
        printf("\n[%d]  Stopped   %s\n", j->id, j->cmdline);
    return status;
}
