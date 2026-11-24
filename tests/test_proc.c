/*
 * test_proc.c - Tests for process management (Person 3):
 * the Round Robin simulation and the job table with real child processes.
 */
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "test.h"
#include "proc.h"

static void test_round_robin(void)
{
    /* Worked example (quantum 2, bursts 5 3 8):
     *   P1 0-2, P2 2-4, P3 4-6, P1 6-8, P2 8-9 done, P3 9-11,
     *   P1 11-12 done, P3 12-14, P3 14-16 done */
    SimProc p[3] = { { "P1", 5, 0, 0, 0 }, { "P2", 3, 0, 0, 0 }, { "P3", 8, 0, 0, 0 } };
    CHECK(sched_rr_run(p, 3, 2, 0) == MS_OK);
    CHECK(p[0].finish_time == 12 && p[0].waiting_time == 7);
    CHECK(p[1].finish_time == 9  && p[1].waiting_time == 6);
    CHECK(p[2].finish_time == 16 && p[2].waiting_time == 8);
    CHECK(p[0].remaining_time == 0 && p[2].remaining_time == 0);

    /* A huge quantum turns Round Robin into First-Come-First-Served. */
    CHECK(sched_rr_run(p, 3, 100, 0) == MS_OK);
    CHECK(p[0].finish_time == 5  && p[0].waiting_time == 0);
    CHECK(p[1].finish_time == 8  && p[1].waiting_time == 5);
    CHECK(p[2].finish_time == 16 && p[2].waiting_time == 8);

    /* quantum 1 with equal bursts: they take turns, finishing in order */
    SimProc q[2] = { { "A", 2, 0, 0, 0 }, { "B", 2, 0, 0, 0 } };
    CHECK(sched_rr_run(q, 2, 1, 0) == MS_OK);
    CHECK(q[0].finish_time == 3 && q[1].finish_time == 4);

    /* bad input */
    CHECK(sched_rr_run(p, 3, 0, 0) == MS_ERR);
    CHECK(sched_rr_run(p, 0, 2, 0) == MS_ERR);
}

/* Starts a child in its own process group; 'what' says what it does. */
enum { EXIT_3, STOP_THEN_EXIT, WAIT_FOREVER, EXIT_NOW };

static pid_t spawn(int what)
{
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        switch (what) {
        case EXIT_3:         _exit(3);
        case STOP_THEN_EXIT: raise(SIGSTOP); _exit(0);
        case WAIT_FOREVER:   for (;;) pause();
        default:             _exit(0);
        }
    }
    setpgid(pid, pid);
    return pid;
}

/* Calls jobs_reap until the table is empty (max ~2 seconds). */
static int reap_until_empty(void)
{
    struct timespec ten_ms = { 0, 10 * 1000 * 1000 };
    for (int i = 0; i < 200 && jobs_count() > 0; i++) {
        jobs_reap(0);
        nanosleep(&ten_ms, NULL);
    }
    return jobs_count() == 0;
}

static void test_jobs(void)
{
    CHECK(jobs_count() == 0);

    /* foreground job: exit code is returned, job is removed */
    pid_t pid = spawn(EXIT_3);
    int id = jobs_add(pid, &pid, 1, "exit 3", JOB_RUNNING);
    CHECK(id == 1);
    CHECK(jobs_find(id) != NULL && jobs_find(id)->pgid == pid);
    CHECK(wait_foreground(id, 0) == 3);
    CHECK(jobs_find(id) == NULL);

    /* a job that stops (like Ctrl+Z) stays in the table... */
    pid = spawn(STOP_THEN_EXIT);
    id = jobs_add(pid, &pid, 1, "stopper", JOB_RUNNING);
    CHECK(wait_foreground(id, 0) == 128 + SIGTSTP);
    CHECK(jobs_find(id) != NULL && jobs_find(id)->state == JOB_STOPPED);
    /* ...and 'fg' (resume = 1) continues it until it finishes */
    CHECK(wait_foreground(id, 1) == 0);
    CHECK(jobs_find(id) == NULL);

    /* background job that is killed: reaped and removed */
    pid = spawn(WAIT_FOREVER);
    id = jobs_add(pid, &pid, 1, "sleeper", JOB_RUNNING);
    CHECK(jobs_count() == 1);
    kill(-pid, SIGTERM);
    CHECK(reap_until_empty());

    /* background job that just finishes */
    pid = spawn(EXIT_NOW);
    jobs_add(pid, &pid, 1, "quick", JOB_RUNNING);
    CHECK(reap_until_empty());

    /* job IDs: the smallest free number is reused */
    pid_t fake = 999999;
    int a = jobs_add(fake, &fake, 1, "a", JOB_STOPPED);
    int b = jobs_add(fake, &fake, 1, "b", JOB_STOPPED);
    CHECK(a == 1 && b == 2);
    memset(jobs_find(a), 0, sizeof(Job));                 /* remove job 1 by hand */
    CHECK(jobs_add(fake, &fake, 1, "c", JOB_STOPPED) == 1);
    memset(jobs_find(1), 0, sizeof(Job));
    memset(jobs_find(2), 0, sizeof(Job));
    CHECK(jobs_count() == 0);
}

void test_proc(void)
{
    test_round_robin();
    test_jobs();
}
