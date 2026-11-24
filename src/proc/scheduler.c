/*
 * scheduler.c - Round Robin scheduling simulation (Person 3).
 *
 * Round Robin is like passing a microphone around a circle: each process
 * may talk for at most 'quantum' time units, then goes to the back of
 * the queue if it still has more to say.
 *
 * Example: sched 2 5 3 8   (quantum 2, three processes)
 *   t=0-2 P1, t=2-4 P2, t=4-6 P3, t=6-8 P1, t=8-9 P2 (done) ...
 *
 * waiting time = finish time - burst time   (everyone arrives at t=0)
 */
#include <stdlib.h>
#include <string.h>
#include "proc.h"

int sched_rr_run(SimProc procs[], int n, int quantum, int verbose)
{
    if (n <= 0 || n > MAX_SIM_PROCS || quantum <= 0)
        return MS_ERR;

    /* The ready queue, as a circular array of process indexes. */
    int queue[MAX_SIM_PROCS];
    int head = 0, count = 0;

    for (int i = 0; i < n; i++) {
        if (procs[i].burst_time <= 0)
            return MS_ERR;
        procs[i].remaining_time = procs[i].burst_time;
        procs[i].finish_time = 0;
        procs[i].waiting_time = 0;
        queue[count++] = i;
    }

    int time = 0;
    while (count > 0) {
        int i = queue[head];                    /* take the front process */
        head = (head + 1) % MAX_SIM_PROCS;
        count--;

        SimProc *p = &procs[i];
        int run = p->remaining_time < quantum ? p->remaining_time : quantum;
        p->remaining_time -= run;

        if (verbose) {
            printf("  t=%3d-%-3d  %-4s runs", time, time + run, p->name);
            if (p->remaining_time > 0)
                printf(" (%d left)\n", p->remaining_time);
            else
                printf(" (finished)\n");
        }
        time += run;

        if (p->remaining_time > 0) {            /* not done: back of the queue */
            queue[(head + count) % MAX_SIM_PROCS] = i;
            count++;
        } else {
            p->finish_time = time;
            p->waiting_time = p->finish_time - p->burst_time;
        }
    }
    return MS_OK;
}

int cmd_sched(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: sched <quantum> <burst1> [burst2 ...]\n"
                        "example: sched 2 5 3 8\n");
        return MS_ERR;
    }
    int quantum = atoi(argv[1]);
    int n = argc - 2;
    if (quantum <= 0 || n > MAX_SIM_PROCS) {
        fprintf(stderr, "sched: quantum must be > 0 and at most %d processes\n",
                MAX_SIM_PROCS);
        return MS_ERR;
    }

    SimProc procs[MAX_SIM_PROCS];
    memset(procs, 0, sizeof procs);
    for (int i = 0; i < n; i++) {
        snprintf(procs[i].name, sizeof procs[i].name, "P%d", i + 1);
        procs[i].burst_time = atoi(argv[i + 2]);
        if (procs[i].burst_time <= 0) {
            fprintf(stderr, "sched: burst times must be > 0\n");
            return MS_ERR;
        }
    }

    printf("Round Robin, quantum = %d\n", quantum);
    sched_rr_run(procs, n, quantum, 1);

    printf("\n  %-4s  %5s  %6s  %10s  %7s\n",
           "PROC", "BURST", "FINISH", "TURNAROUND", "WAITING");
    double total_wait = 0, total_turn = 0;
    for (int i = 0; i < n; i++) {
        printf("  %-4s  %5d  %6d  %10d  %7d\n", procs[i].name, procs[i].burst_time,
               procs[i].finish_time, procs[i].finish_time, procs[i].waiting_time);
        total_wait += procs[i].waiting_time;
        total_turn += procs[i].finish_time;   /* turnaround = finish - arrival(0) */
    }
    printf("\n  Average waiting time:    %.2f\n", total_wait / n);
    printf("  Average turnaround time: %.2f\n", total_turn / n);
    return MS_OK;
}
