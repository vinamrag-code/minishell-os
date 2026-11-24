/*
 * jobs.c - The job table and jobs/ps/fg/bg/kill (Person 3).
 *
 * A job = all processes started by one command line. Its life:
 *
 *   RUNNING --Ctrl+Z--> STOPPED --fg/bg--> RUNNING --exits--> DONE (removed)
 *
 * waitpid() tells us when a child changes state:
 *   WIFEXITED   it finished normally      WIFSIGNALED  it was killed
 *   WIFSTOPPED  it was paused (Ctrl+Z)    WIFCONTINUED it was resumed
 */
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "proc.h"

static Job jobs[MAX_JOBS];          /* slot is free when id == 0 */

/* ---------- table helpers ---------- */

Job *jobs_find(int job_id)
{
    if (job_id <= 0)
        return NULL;
    for (int i = 0; i < MAX_JOBS; i++)
        if (jobs[i].id == job_id)
            return &jobs[i];
    return NULL;
}

int jobs_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_JOBS; i++)
        if (jobs[i].id != 0)
            n++;
    return n;
}

int jobs_add(pid_t pgid, const pid_t pids[], int npids,
             const char *cmdline, JobState state)
{
    if (npids <= 0 || npids > JOB_MAX_PROCS)
        return MS_ERR;

    Job *slot = NULL;
    for (int i = 0; i < MAX_JOBS && slot == NULL; i++)
        if (jobs[i].id == 0)
            slot = &jobs[i];
    if (slot == NULL)
        return MS_ERR;

    int id = 1;                     /* smallest job number not in use */
    while (jobs_find(id) != NULL)
        id++;

    memset(slot, 0, sizeof *slot);
    slot->id = id;
    slot->pgid = pgid;
    slot->npids = npids;
    slot->alive = npids;
    slot->state = state;
    for (int i = 0; i < npids; i++) {
        slot->pids[i] = pids[i];
        slot->pid_alive[i] = 1;
    }
    strncpy(slot->cmdline, cmdline, MAX_LINE - 1);
    return id;
}

static void jobs_remove(Job *j)
{
    memset(j, 0, sizeof *j);
}

/* Updates the job that owns 'pid' after waitpid() returned 'status'. */
static void record_status(pid_t pid, int status)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &jobs[i];
        if (j->id == 0)
            continue;
        for (int k = 0; k < j->npids; k++) {
            if (j->pids[k] != pid)
                continue;
            if (WIFSTOPPED(status)) {
                j->state = JOB_STOPPED;
            } else if (WIFCONTINUED(status)) {
                j->state = JOB_RUNNING;
            } else {                          /* exited or killed */
                if (j->pid_alive[k]) {
                    j->pid_alive[k] = 0;
                    j->alive--;
                }
                if (k == j->npids - 1)        /* the job's result = last command's */
                    j->last_status = status;
                if (j->alive == 0)
                    j->state = JOB_DONE;
            }
            return;
        }
    }
}

/* Raw wait status -> shell exit code (0..255). */
static int exit_code(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 0;
}

static const char *state_name(JobState s)
{
    switch (s) {
    case JOB_RUNNING: return "Running";
    case JOB_STOPPED: return "Stopped";
    default:          return "Done";
    }
}

/* ---------- waiting ---------- */

void jobs_reap(int report)
{
    int status;
    pid_t pid;
    /* WNOHANG = don't wait, just tell me about children that already changed. */
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0)
        record_status(pid, status);

    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &jobs[i];
        if (j->id != 0 && j->state == JOB_DONE) {
            if (report)
                printf("[%d]  %s   %s\n", j->id,
                       WIFSIGNALED(j->last_status) ? "Terminated" : "Done",
                       j->cmdline);
            jobs_remove(j);
        }
    }
}

int wait_foreground(int job_id, int resume)
{
    Job *j = jobs_find(job_id);
    if (j == NULL)
        return 1;

    if (proc_interactive())
        tcsetpgrp(STDIN_FILENO, j->pgid);   /* the job gets the keyboard */

    if (resume) {                           /* give the terminal FIRST, then wake it */
        kill(-j->pgid, SIGCONT);
        j->state = JOB_RUNNING;
    }

    while (j->state == JOB_RUNNING && j->alive > 0) {
        int status;
        /* -pgid = any process in this job; WUNTRACED = also report Ctrl+Z */
        pid_t pid = waitpid(-j->pgid, &status, WUNTRACED);
        if (pid < 0) {
            if (errno == EINTR)
                continue;
            j->alive = 0;                   /* nothing left to wait for */
            j->state = JOB_DONE;
            break;
        }
        record_status(pid, status);
    }

    if (proc_interactive())
        tcsetpgrp(STDIN_FILENO, getpgrp()); /* the shell takes the keyboard back */

    if (j->state == JOB_STOPPED)
        return 128 + SIGTSTP;

    int code = exit_code(j->last_status);
    if (WIFSIGNALED(j->last_status) && WTERMSIG(j->last_status) == SIGINT)
        printf("\n");                       /* after ^C, start on a new line */
    jobs_remove(j);
    return code;
}

void jobs_kill_all(void)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].id != 0) {
            kill(-jobs[i].pgid, SIGHUP);
            kill(-jobs[i].pgid, SIGCONT);   /* a stopped job must wake to die */
        }
    }
}

/* ---------- built-in commands ---------- */

/* Reads "2" or "%2". With no argument, picks the newest job. */
static Job *job_from_args(int argc, char **argv, const char *cmd)
{
    if (argc < 2) {
        Job *newest = NULL;
        for (int i = 0; i < MAX_JOBS; i++)
            if (jobs[i].id != 0 && (newest == NULL || jobs[i].id > newest->id))
                newest = &jobs[i];
        if (newest == NULL)
            fprintf(stderr, "%s: no current job\n", cmd);
        return newest;
    }
    const char *s = argv[1];
    if (s[0] == '%')
        s++;
    Job *j = jobs_find(atoi(s));
    if (j == NULL)
        fprintf(stderr, "%s: %s: no such job\n", cmd, argv[1]);
    return j;
}

int cmd_jobs(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    jobs_reap(1);
    for (int i = 0; i < MAX_JOBS; i++)
        if (jobs[i].id != 0)
            printf("[%d]  %-8s  %s\n", jobs[i].id, state_name(jobs[i].state),
                   jobs[i].cmdline);
    return MS_OK;
}

int cmd_ps(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    printf("%7s  %4s  %-8s  %s\n", "PID", "JOB", "STATE", "COMMAND");
    printf("%7d  %4s  %-8s  %s\n", (int)getpid(), "-", "Running", "minishell");
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &jobs[i];
        if (j->id == 0)
            continue;
        for (int k = 0; k < j->npids; k++)
            if (j->pid_alive[k])
                printf("%7d  [%2d]  %-8s  %s\n", (int)j->pids[k], j->id,
                       state_name(j->state), j->cmdline);
    }
    return MS_OK;
}

int cmd_fg(int argc, char **argv)
{
    Job *j = job_from_args(argc, argv, "fg");
    if (j == NULL)
        return MS_ERR;
    int id = j->id;
    printf("%s\n", j->cmdline);
    wait_foreground(id, 1);
    j = jobs_find(id);
    if (j != NULL && j->state == JOB_STOPPED)
        printf("\n[%d]  Stopped   %s\n", j->id, j->cmdline);
    return MS_OK;
}

int cmd_bg(int argc, char **argv)
{
    Job *j = job_from_args(argc, argv, "bg");
    if (j == NULL)
        return MS_ERR;
    if (j->state != JOB_STOPPED) {
        fprintf(stderr, "bg: job %d is already running\n", j->id);
        return MS_ERR;
    }
    kill(-j->pgid, SIGCONT);
    j->state = JOB_RUNNING;
    printf("[%d]  %s &\n", j->id, j->cmdline);
    return MS_OK;
}

int cmd_kill(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: kill <job id>\n");
        return MS_ERR;
    }
    Job *j = job_from_args(argc, argv, "kill");
    if (j == NULL)
        return MS_ERR;
    kill(-j->pgid, SIGTERM);
    if (j->state == JOB_STOPPED)
        kill(-j->pgid, SIGCONT);            /* it must wake up to receive SIGTERM */
    return MS_OK;
}
