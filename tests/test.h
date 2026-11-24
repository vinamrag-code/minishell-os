/*
 * test.h - A tiny home-made test framework.
 *
 * Usage inside a test function:
 *
 *     CHECK(1 + 1 == 2);
 *
 * If the condition is false, it prints the file, line and condition,
 * and counts a failure. The test program exits with code 1 if anything
 * failed, so 'make test' stops with an error.
 */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>

extern int tests_run;
extern int tests_failed;

#define CHECK(cond) do {                                                  \
        tests_run++;                                                      \
        if (!(cond)) {                                                    \
            tests_failed++;                                               \
            printf("\n    FAIL  %s:%d: %s", __FILE__, __LINE__, #cond);   \
        }                                                                 \
    } while (0)

/* Hide stderr while testing things that are SUPPOSED to print errors.
 * quiet_stderr(1) = hide, quiet_stderr(0) = show again. */
void quiet_stderr(int on);

/* Reads a whole real file into buf (adds '\0'). Returns bytes read or -1. */
int read_real_file(const char *path, char *buf, int size);

/* One test group per module. Each lives in its own tests/test_*.c file. */
void test_shell(void);
void test_fs(void);
void test_proc(void);
void test_mem(void);

#endif /* TEST_H */
