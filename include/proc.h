/*
 * proc.h - Process management interface (Person 3).
 *
 * Two separate parts:
 *
 *  1. REAL processes: the programs the shell starts with fork/exec.
 *     Each command line becomes a "job" (one job = all the processes of
 *     one pipeline, sharing one process group). The user can run 'jobs',
 *     'fg', 'bg' and 'kill', and press Ctrl+C / Ctrl+Z.
 *
 *  2. SIMULATED processes: pretend processes with a made-up running time,
 *     used to show how a Round Robin scheduler shares the CPU.
 *     Nothing is really run.
 */
#ifndef PROC_H
#define PROC_H

#include <sys/types.h>   /* pid_t */
#include "common.h"

/* ======================= Part 1: Job control ============================ */

#define MAX_JOBS       32
#define JOB_MAX_PROCS  8     /* most processes in one job (one pipeline) */

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,   /* paused with Ctrl+Z, can be resumed with fg/bg  */
    JOB_DONE       /* finished; removed after it has been reported   */
} JobState;

typedef struct {
    int      id;                         /* number the user types: fg 1; 0 = free slot */
    pid_t    pgid;                       /* process group shared by the job */
    pid_t    pids[JOB_MAX_PROCS];        /* every process in the pipeline   */
    int      pid_alive[JOB_MAX_PROCS];   /* 1 until that process ends       */
    int      npids;
    int      alive;                      /* how many processes still exist  */
    JobState state;
    int      last_status;                /* raw wait() status of the last process */
    char     cmdline[MAX_LINE];          /* what the user typed             */
} Job;

/* ---- Signals (signals.c) ----
 * In an interactive terminal: puts the shell in its own process group,
 * makes Ctrl+C at the prompt just start a new line, and makes the shell
 * ignore Ctrl+Z, so that only the program in front gets those keys.
 * Finished children are collected by jobs_reap() before each prompt
 * (we "poll" instead of using a SIGCHLD handler, which is much simpler). */
int  signals_init(void);
void signals_reset_child(void);   /* call in a child after fork, before exec */
int  signals_take_interrupt(void);/* 1 if Ctrl+C was pressed at the prompt (and clears it) */
int  proc_interactive(void);      /* 1 if we control a real terminal */

/* ---- Job table (jobs.c) ---- */

/* Adds a job. Returns its new job ID, or MS_ERR if the table is full. */
int jobs_add(pid_t pgid, const pid_t pids[], int npids,
             const char *cmdline, JobState state);

Job *jobs_find(int job_id);   /* NULL if there is no such job */
int  jobs_count(void);        /* how many jobs are in the table */

/* Collects finished/stopped children without waiting. Jobs that are done
 * are removed; if report = 1 they are printed, e.g. "[1]  Done   sleep 5". */
void jobs_reap(int report);

/* Gives the job the terminal and waits until it finishes or is stopped.
 * resume = 1 first sends SIGCONT (used by 'fg' on a stopped job).
 * A finished job is removed from the table; a stopped one stays.
 * Returns the exit status (128 + signal number if killed or stopped). */
int wait_foreground(int job_id, int resume);

/* Hangs up all jobs (used when the shell exits). */
void jobs_kill_all(void);

/* Built-in commands */
int cmd_jobs(int argc, char **argv);   /* jobs                  */
int cmd_ps(int argc, char **argv);     /* ps                    */
int cmd_fg(int argc, char **argv);     /* fg [job id]           */
int cmd_bg(int argc, char **argv);     /* bg [job id]           */
int cmd_kill(int argc, char **argv);   /* kill <job id>         */

/* ================= Part 2: Round Robin simulation ======================= */

#define MAX_SIM_PROCS 16

typedef struct {
    char name[16];        /* e.g. "P1"                                   */
    int  burst_time;      /* total CPU time it needs (input)             */
    int  remaining_time;  /* CPU time still needed (changes while running) */
    int  finish_time;     /* time it completed (output)                  */
    int  waiting_time;    /* time it spent waiting in the queue (output) */
} SimProc;

/* Runs Round Robin over 'n' processes that all arrive at time 0.
 * Fill in name and burst_time; the rest is computed.
 * Each process gets at most 'quantum' time units per turn.
 * If verbose = 1, prints each step, e.g. "t=0-2   P1 runs (3 left)". */
int sched_rr_run(SimProc procs[], int n, int quantum, int verbose);

/* Built-in: sched <quantum> <burst1> <burst2> ...   e.g. sched 2 5 3 8 */
int cmd_sched(int argc, char **argv);

#endif /* PROC_H */
