/*
 * mem.h - Custom memory allocator interface (Person 4).
 *
 * We take one fixed array of bytes (the "pool") and hand out pieces
 * of it, like our own malloc/free. The shell's parser stores every word
 * you type in this pool, so 'memstat' shows real usage.
 *
 * The pool is a list of blocks. Each block has a small header followed
 * by its data, and is either used or free:
 *
 *   [hdr|used 100][hdr|free 50][hdr|used 200][hdr|free 400 ..........]
 *
 * mem_alloc uses FIRST-FIT: it walks the list from the start and takes
 * the first free block that is big enough, splitting it if there is
 * plenty left over. mem_free marks a block free and joins (coalesces)
 * it with free neighbours.
 */
#ifndef MEM_H
#define MEM_H

#include <stddef.h>   /* size_t */
#include "common.h"

#define MEM_POOL_SIZE (64 * 1024)   /* 64 KB pool */
#define MEM_ALIGN     16            /* every pointer is a multiple of 16 */

/* Resets the pool to one big free block. Call once at startup
 * (and in tests, to start from a clean pool). */
int mem_init(void);

/* Like malloc: returns a pointer to at least 'size' bytes inside the pool,
 * or NULL if size is 0 or no free block is big enough. */
void *mem_alloc(size_t size);

/* Like free: gives the memory back. mem_free(NULL) does nothing.
 * Prints a warning for pointers that did not come from mem_alloc. */
void mem_free(void *ptr);

/* A snapshot of the pool, used by 'memstat' and by the tests. */
typedef struct {
    size_t total_bytes;     /* MEM_POOL_SIZE                          */
    size_t used_bytes;      /* data bytes inside used blocks          */
    size_t free_bytes;      /* data bytes inside free blocks          */
    size_t largest_free;    /* biggest single free block              */
    int    used_blocks;     /* how many used blocks                   */
    int    free_blocks;     /* how many free blocks                   */
    double fragmentation;   /* 0.0 = all free memory is one piece,
                               near 1.0 = free memory is in tiny pieces.
                               = 1 - largest_free / free_bytes        */
} MemStats;

void mem_get_stats(MemStats *out);

/* Built-in commands */
int cmd_memstat(int argc, char **argv);   /* memstat: usage, fragmentation, map */
int cmd_memalloc(int argc, char **argv);  /* memalloc <bytes>: demo allocation  */
int cmd_memfree(int argc, char **argv);   /* memfree <handle>: free a demo block */

#endif /* MEM_H */
