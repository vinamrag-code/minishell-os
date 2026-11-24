/*
 * test_mem.c - Tests for the first-fit allocator and perm_check (Person 4).
 */
#include <stdint.h>
#include <string.h>

#include "test.h"
#include "mem.h"
#include "users.h"

static void test_allocator(void)
{
    MemStats s;
    CHECK(mem_init() == MS_OK);
    mem_get_stats(&s);
    CHECK(s.total_bytes == MEM_POOL_SIZE);
    CHECK(s.free_blocks == 1 && s.used_blocks == 0);
    CHECK(s.fragmentation == 0.0);
    size_t free_at_start = s.free_bytes;

    /* three blocks, one after another, all aligned */
    char *a = mem_alloc(100);
    char *b = mem_alloc(200);
    char *c = mem_alloc(300);
    CHECK(a != NULL && b != NULL && c != NULL);
    CHECK(a < b && b < c);
    CHECK((uintptr_t)a % MEM_ALIGN == 0 && (uintptr_t)b % MEM_ALIGN == 0);
    memset(a, 'A', 100);                       /* writing must not damage b */
    memset(b, 'B', 200);
    memset(c, 'C', 300);
    CHECK(a[99] == 'A' && b[0] == 'B' && b[199] == 'B' && c[0] == 'C');
    mem_get_stats(&s);
    CHECK(s.used_blocks == 3);

    /* freeing the middle block leaves a hole -> fragmentation */
    mem_free(b);
    mem_get_stats(&s);
    CHECK(s.free_blocks == 2);
    CHECK(s.fragmentation > 0.0);

    /* FIRST-FIT: a small request reuses the hole */
    char *d = mem_alloc(50);
    CHECK(d == b);
    /* a request bigger than the hole goes after c instead */
    char *e = mem_alloc(500);
    CHECK(e > c);

    /* free everything: neighbours merge back into ONE free block */
    mem_free(a);
    mem_free(d);
    mem_free(e);
    mem_free(c);
    mem_get_stats(&s);
    CHECK(s.free_blocks == 1 && s.used_blocks == 0);
    CHECK(s.free_bytes == free_at_start);
    CHECK(s.fragmentation == 0.0);

    /* impossible requests */
    CHECK(mem_alloc(0) == NULL);
    CHECK(mem_alloc(MEM_POOL_SIZE) == NULL);
    mem_free(NULL);                            /* allowed, does nothing */

    /* fill the pool completely, then free it all */
    void *blocks[100];
    int count = 0;
    while (count < 100 && (blocks[count] = mem_alloc(1024)) != NULL)
        count++;
    CHECK(count > 50 && count < 100);          /* 64 KB / (1 KB + header) */
    CHECK(mem_alloc(1024) == NULL);
    for (int i = 0; i < count; i += 2)         /* free every other block */
        mem_free(blocks[i]);
    mem_get_stats(&s);
    CHECK(s.fragmentation > 0.5);              /* lots of small holes */
    CHECK(mem_alloc(4096) == NULL);            /* enough in total, but no single hole fits */
    for (int i = 1; i < count; i += 2)
        mem_free(blocks[i]);
    mem_get_stats(&s);
    CHECK(s.free_blocks == 1 && s.free_bytes == free_at_start);

    /* bad frees are reported (quietly here) and change nothing */
    char *x = mem_alloc(10);
    quiet_stderr(1);
    mem_free(x + 1);                           /* not a block start */
    mem_free(x);
    mem_free(x);                               /* double free */
    quiet_stderr(0);
    mem_get_stats(&s);
    CHECK(s.free_blocks == 1 && s.used_blocks == 0);
}

static void test_perm_check(void)
{
    users_init();
    CHECK(strcmp(current_user()->name, "root") == 0);
    CHECK(perm_check(1000, 100, 0000, PERM_READ | PERM_WRITE) == 1);   /* root */

    CHECK(users_login("alice", "wrong") == MS_ERR);
    CHECK(users_login("nobody", "x") == MS_ERR);
    CHECK(strcmp(current_user()->name, "root") == 0);   /* unchanged */
    CHECK(users_login("alice", "alice") == MS_OK);      /* uid 1000, gid 100 */

    /* owner bits */
    CHECK(perm_check(1000, 100, 0640, PERM_READ) == 1);
    CHECK(perm_check(1000, 100, 0640, PERM_WRITE) == 1);
    CHECK(perm_check(1000, 100, 0640, PERM_EXEC) == 0);
    CHECK(perm_check(1000, 100, 0640, PERM_READ | PERM_WRITE) == 1);
    CHECK(perm_check(1000, 100, 0440, PERM_READ | PERM_WRITE) == 0);
    /* group bits (someone else's file, same group) */
    CHECK(perm_check(1001, 100, 0640, PERM_READ) == 1);
    CHECK(perm_check(1001, 100, 0640, PERM_WRITE) == 0);
    /* other bits (different owner and group) */
    CHECK(perm_check(1002, 200, 0640, PERM_READ) == 0);
    CHECK(perm_check(1002, 200, 0604, PERM_READ) == 1);
    /* only ONE set applies: owner with --- can't read even if others can */
    CHECK(perm_check(1000, 100, 0077, PERM_READ) == 0);

    CHECK(strcmp(users_name(1001), "bob") == 0);
    CHECK(strcmp(users_group_name(200), "staff") == 0);
    CHECK(strcmp(users_name(4242), "?") == 0);
    users_init();
}

void test_mem(void)
{
    CHECK((PERM_READ | PERM_WRITE | PERM_EXEC) == 7);
    test_allocator();
    test_perm_check();
}
