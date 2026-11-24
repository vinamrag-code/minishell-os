/*
 * test_main.c - Runs every test group and prints a summary.
 */
#include <fcntl.h>
#include <unistd.h>
#include "test.h"

int tests_run = 0;
int tests_failed = 0;

static int saved_stderr = -1;

void quiet_stderr(int on)
{
    fflush(stderr);
    if (on && saved_stderr < 0) {
        saved_stderr = dup(STDERR_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    } else if (!on && saved_stderr >= 0) {
        dup2(saved_stderr, STDERR_FILENO);
        close(saved_stderr);
        saved_stderr = -1;
    }
}

int read_real_file(const char *path, char *buf, int size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    int n = (int)fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
    return n;
}

/* Runs one group and prints how many of its checks failed. */
static void run_group(const char *name, void (*group)(void))
{
    int failed_before = tests_failed;
    int run_before = tests_run;

    printf("  %-6s ", name);
    fflush(stdout);
    group();

    int run = tests_run - run_before;
    int failed = tests_failed - failed_before;
    if (failed == 0)
        printf("ok      (%d checks)\n", run);
    else
        printf("\n  %-6s FAILED  (%d of %d checks)\n", name, failed, run);
}

int main(void)
{
    printf("Unit tests:\n");
    run_group("shell", test_shell);
    run_group("fs",    test_fs);
    run_group("proc",  test_proc);
    run_group("mem",   test_mem);

    printf("%d checks, %d failed\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
