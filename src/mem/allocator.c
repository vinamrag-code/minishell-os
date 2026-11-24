/*
 * allocator.c - First-fit malloc/free over a fixed pool (Person 4).
 *
 * The pool is one big array. We cut it into blocks; each block starts
 * with a header that says how big it is, whether it is free, and where
 * the next block is (a linked list in address order):
 *
 *   pool: [H|data....][H|data..][H|data..............]
 *          ^ head      ^ next    ^ next -> NULL
 *
 * FRAGMENTATION: after many allocs and frees, free memory can end up in
 * many small pieces. Like empty seats scattered around a cinema: there
 * may be 10 free seats in total, but a group of 4 still can't sit
 * together. 'memstat' measures this.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mem.h"

typedef struct Block {
    size_t        size;    /* bytes of data after the header */
    int           free;    /* 1 = free, 0 = used             */
    struct Block *next;    /* next block in the pool, or NULL */
} Block;

/* Header size rounded up so that data always starts 16-byte aligned. */
#define HEADER_SIZE  ((sizeof(Block) + MEM_ALIGN - 1) / MEM_ALIGN * MEM_ALIGN)
/* Don't split off a leftover smaller than this (it would be useless). */
#define MIN_SPLIT    (HEADER_SIZE + MEM_ALIGN)

static _Alignas(MEM_ALIGN) unsigned char pool[MEM_POOL_SIZE];
static Block *head = NULL;

static void *data_of(Block *b)
{
    return (unsigned char *)b + HEADER_SIZE;
}

static size_t offset_of(const void *p)
{
    return (size_t)((const unsigned char *)p - pool);
}

int mem_init(void)
{
    head = (Block *)pool;
    head->size = MEM_POOL_SIZE - HEADER_SIZE;
    head->free = 1;
    head->next = NULL;
    return MS_OK;
}

void *mem_alloc(size_t size)
{
    if (size == 0 || head == NULL)
        return NULL;
    if (size > MEM_POOL_SIZE)
        return NULL;
    size = (size + MEM_ALIGN - 1) / MEM_ALIGN * MEM_ALIGN;   /* round up */

    /* FIRST-FIT: the first free block that is big enough. */
    for (Block *b = head; b != NULL; b = b->next) {
        if (!b->free || b->size < size)
            continue;

        /* Split if the leftover is big enough to be a useful block. */
        if (b->size >= size + MIN_SPLIT) {
            Block *rest = (Block *)((unsigned char *)b + HEADER_SIZE + size);
            rest->size = b->size - size - HEADER_SIZE;
            rest->free = 1;
            rest->next = b->next;
            b->next = rest;
            b->size = size;
        }
        b->free = 0;
        return data_of(b);
    }
    return NULL;                         /* no block big enough */
}

void mem_free(void *ptr)
{
    if (ptr == NULL)
        return;

    /* Find the block (and the one before it, for merging). */
    Block *prev = NULL, *b = head;
    while (b != NULL && data_of(b) != ptr) {
        prev = b;
        b = b->next;
    }
    if (b == NULL) {
        fprintf(stderr, "mem_free: %p was not returned by mem_alloc\n", ptr);
        return;
    }
    if (b->free) {
        fprintf(stderr, "mem_free: block at offset %zu is already free (double free)\n",
                offset_of(b));
        return;
    }
    b->free = 1;

    /* COALESCE: join with the next block if it is free... */
    if (b->next != NULL && b->next->free) {
        b->size += HEADER_SIZE + b->next->size;
        b->next = b->next->next;
    }
    /* ...and with the previous block if it is free. */
    if (prev != NULL && prev->free) {
        prev->size += HEADER_SIZE + b->size;
        prev->next = b->next;
    }
}

void mem_get_stats(MemStats *out)
{
    memset(out, 0, sizeof *out);
    out->total_bytes = MEM_POOL_SIZE;
    for (Block *b = head; b != NULL; b = b->next) {
        if (b->free) {
            out->free_bytes += b->size;
            out->free_blocks++;
            if (b->size > out->largest_free)
                out->largest_free = b->size;
        } else {
            out->used_bytes += b->size;
            out->used_blocks++;
        }
    }
    if (out->free_bytes > 0)
        out->fragmentation = 1.0 - (double)out->largest_free / (double)out->free_bytes;
}

/* ======================= Built-in commands ============================== */

/* Demo blocks made with 'memalloc', so 'memfree <handle>' can free them. */
#define MAX_DEMO 32
static void *demo[MAX_DEMO];

int cmd_memstat(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    MemStats s;
    mem_get_stats(&s);

    printf("Memory pool: %zu bytes (header per block: %zu bytes)\n",
           s.total_bytes, (size_t)HEADER_SIZE);
    printf("  used : %6zu bytes in %d block(s)\n", s.used_bytes, s.used_blocks);
    printf("  free : %6zu bytes in %d block(s), largest %zu\n",
           s.free_bytes, s.free_blocks, s.largest_free);
    printf("  fragmentation: %.1f%%\n", s.fragmentation * 100.0);

    /* A 64-character map: each character = 1 KB of the pool. */
    printf("  map  : [");
    const size_t step = MEM_POOL_SIZE / 64;
    for (size_t pos = 0; pos < MEM_POOL_SIZE; pos += step) {
        char c = '.';
        for (Block *b = head; b != NULL; b = b->next) {
            size_t start = offset_of(b);
            size_t end = start + HEADER_SIZE + b->size;
            if (pos >= start && pos < end) {
                c = b->free ? '.' : '#';
                break;
            }
        }
        putchar(c);
    }
    printf("]  # used  . free\n");

    printf("  blocks:\n");
    int shown = 0;
    for (Block *b = head; b != NULL && shown < 20; b = b->next, shown++)
        printf("    offset %6zu  %-4s  %6zu bytes\n", offset_of(b),
               b->free ? "free" : "used", b->size);
    if (shown == 20)
        printf("    ...\n");
    printf("  (the parser keeps the words of the current command in the pool)\n");
    return MS_OK;
}

int cmd_memalloc(int argc, char **argv)
{
    if (argc < 2 || atoi(argv[1]) <= 0) {
        fprintf(stderr, "usage: memalloc <bytes>\n");
        return MS_ERR;
    }
    int h = 0;
    while (h < MAX_DEMO && demo[h] != NULL)
        h++;
    if (h == MAX_DEMO) {
        fprintf(stderr, "memalloc: too many demo blocks, free some first\n");
        return MS_ERR;
    }
    void *p = mem_alloc((size_t)atoi(argv[1]));
    if (p == NULL) {
        fprintf(stderr, "memalloc: no free block big enough\n");
        return MS_ERR;
    }
    demo[h] = p;
    printf("handle %d: %s bytes at offset %zu\n", h + 1, argv[1], offset_of(p));
    return MS_OK;
}

int cmd_memfree(int argc, char **argv)
{
    int h = (argc >= 2) ? atoi(argv[1]) - 1 : -1;
    if (h < 0 || h >= MAX_DEMO || demo[h] == NULL) {
        fprintf(stderr, "usage: memfree <handle>   (a handle printed by memalloc)\n");
        return MS_ERR;
    }
    mem_free(demo[h]);
    demo[h] = NULL;
    printf("handle %d freed\n", h + 1);
    return MS_OK;
}
