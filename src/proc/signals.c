/*
 * signals.c - Ctrl+C, Ctrl+Z and terminal control (Person 3).
 *
 * A signal is a short message the OS sends to a process, like a tap on
 * the shoulder. Pressing Ctrl+C sends SIGINT and Ctrl+Z sends SIGTSTP to
 * every process in the terminal's FOREGROUND process group.
 *
 * The trick a shell uses: each job gets its own process group, and the
 * shell hands the terminal to that group while the job runs
 * (tcsetpgrp). So Ctrl+C reaches the job, not the shell.
 */
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "proc.h"

static int interactive = 0;
static volatile sig_atomic_t got_sigint = 0;

/* Runs when Ctrl+C is pressed at the prompt. Only async-signal-safe calls
 * (like write) are allowed inside a signal handler, so no printf. */
static void on_sigint(int sig)
{
    UNUSED(sig);
    got_sigint = 1;
    (void)write(STDOUT_FILENO, "\n", 1);
}

int signals_init(void)
{
    interactive = isatty(STDIN_FILENO);
    if (!interactive)
        return MS_OK;            /* reading a script: keep default behaviour */

    /* If we were started in the background, wait until we are in front. */
    while (tcgetpgrp(STDIN_FILENO) != getpgrp())
        kill(-getpgrp(), SIGTTIN);

    /* Ctrl+C at the prompt: handler, WITHOUT SA_RESTART, so fgets() stops
     * and main() can show a fresh prompt. */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    /* The shell itself ignores these: */
    signal(SIGQUIT, SIG_IGN);    /* Ctrl+\                              */
    signal(SIGTSTP, SIG_IGN);    /* Ctrl+Z: the shell must never stop   */
    signal(SIGTTIN, SIG_IGN);    /* reading/writing the terminal while  */
    signal(SIGTTOU, SIG_IGN);    /* not in front (needed for tcsetpgrp) */

    /* Own process group, and take the terminal. */
    setpgid(0, 0);
    tcsetpgrp(STDIN_FILENO, getpgrp());
    return MS_OK;
}

void signals_reset_child(void)
{
    signal(SIGINT,  SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}

int signals_take_interrupt(void)
{
    int was = got_sigint;
    got_sigint = 0;
    return was;
}

int proc_interactive(void)
{
    return interactive;
}
