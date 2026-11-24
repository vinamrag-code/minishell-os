/*
 * common.h - Things every module shares.
 *
 * Keep this file small. Only put something here if two or more
 * modules really need it.
 */
#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>

/* ---- Return codes ------------------------------------------------------
 * Every function that can fail returns one of these.
 * Success = 0, failure = -1 (the same convention most Linux calls use). */
#define MS_OK    0
#define MS_ERR (-1)

/* ---- Size limits -------------------------------------------------------
 * Fixed sizes keep the code simple: no resizing arrays. */
#define MAX_LINE  1024   /* longest command line the user can type      */
#define MAX_ARGS  64     /* most words in one command (argv entries)    */
#define MAX_PATH  256    /* longest path, e.g. "/home/alice/notes.txt"  */

/* ---- The shape of every built-in command -------------------------------
 * Every built-in command (ls, cd, jobs, memstat, ...) is a function that
 * looks exactly like a C main():
 *
 *     int cmd_ls(int argc, char **argv);
 *
 * argv[0] is the command name, argv[1..argc-1] are its arguments.
 * Return MS_OK on success, MS_ERR on failure.
 *
 * Because they all have the same shape, the shell can keep them in one
 * lookup table and call any of them the same way. */
typedef int (*command_fn)(int argc, char **argv);

/* Silences the "unused parameter" warning, e.g. when a command
 * ignores its arguments. */
#define UNUSED(x) ((void)(x))

#endif /* COMMON_H */
