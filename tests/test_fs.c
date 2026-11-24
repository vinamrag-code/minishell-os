/*
 * test_fs.c - Tests for the virtual file system (Person 2)
 * and for file permissions (Person 4).
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test.h"
#include "fs.h"
#include "users.h"

static char buf[FS_MAX_FILE_SIZE + 1];

/* Reads a VFS file into buf as a C string. Returns its length or MS_ERR. */
static int read_str(const char *path)
{
    int n = vfs_read(path, buf, FS_MAX_FILE_SIZE);
    buf[n < 0 ? 0 : n] = '\0';
    return n;
}

static void test_geometry(void)
{
    CHECK(sizeof(DirEntry) == 32);
    CHECK(FS_BLOCK_SIZE % sizeof(DirEntry) == 0);  /* entries never cross blocks */
}

static void test_basic(void)
{
    CHECK(vfs_lookup("/") == FS_ROOT_INODE);
    CHECK(vfs_lookup("/home/alice") >= 0);
    CHECK(vfs_lookup("/tmp") >= 0);
    CHECK(vfs_lookup("/nope") == MS_ERR);

    /* directories */
    CHECK(vfs_mkdir("/projects") == MS_OK);
    CHECK(vfs_mkdir("/projects") == MS_ERR);              /* already exists */
    CHECK(vfs_mkdir("/missing/dir") == MS_ERR);           /* no parent */
    CHECK(vfs_get_inode(vfs_lookup("/projects"))->type == INODE_DIR);

    /* files: write, append, overwrite */
    CHECK(vfs_create("/projects/a.txt") == MS_OK);
    CHECK(vfs_write("/projects/a.txt", "hello", 5, 0) == MS_OK);
    CHECK(read_str("/projects/a.txt") == 5 && strcmp(buf, "hello") == 0);
    CHECK(vfs_write("/projects/a.txt", " world", 6, 1) == MS_OK);
    CHECK(strcmp((read_str("/projects/a.txt"), buf), "hello world") == 0);
    CHECK(vfs_write("/projects/a.txt", "hi", 2, 0) == MS_OK);
    CHECK(strcmp((read_str("/projects/a.txt"), buf), "hi") == 0);
    CHECK(vfs_get_inode(vfs_lookup("/projects/a.txt"))->size == 2);

    /* vfs_write creates missing files */
    CHECK(vfs_write("/projects/new.txt", "x", 1, 0) == MS_OK);
    CHECK(vfs_lookup("/projects/new.txt") >= 0);

    /* errors */
    CHECK(vfs_mkdir("/projects/a.txt/x") == MS_ERR);      /* a file is not a dir */
    CHECK(vfs_chdir("/projects/a.txt") == MS_ERR);
    CHECK(vfs_read("/projects", buf, 10) == MS_ERR);      /* can't cat a dir */
    CHECK(strcmp(vfs_last_error(), "Is a directory") == 0);
    CHECK(vfs_mkdir("/projects/a_name_that_is_far_too_long_for_us") == MS_ERR);
}

static void test_cwd(void)
{
    char cwd[MAX_PATH];
    CHECK(vfs_chdir("/projects") == MS_OK);
    CHECK(vfs_getcwd(cwd, sizeof cwd) == MS_OK && strcmp(cwd, "/projects") == 0);

    CHECK(vfs_mkdir("sub") == MS_OK);                     /* relative path */
    CHECK(vfs_chdir("sub") == MS_OK);
    CHECK(vfs_getcwd(cwd, sizeof cwd) == MS_OK && strcmp(cwd, "/projects/sub") == 0);
    CHECK(read_str("../a.txt") == 2);                     /* ".." works */
    CHECK(vfs_chdir("../..") == MS_OK);
    CHECK(vfs_getcwd(cwd, sizeof cwd) == MS_OK && strcmp(cwd, "/") == 0);
    CHECK(vfs_chdir("..") == MS_OK);                      /* root's parent is root */
    CHECK(vfs_getcwd(cwd, sizeof cwd) == MS_OK && strcmp(cwd, "/") == 0);

    /* can't remove the directory you are standing in */
    CHECK(vfs_chdir("/projects/sub") == MS_OK);
    CHECK(vfs_remove("/projects/sub") == MS_ERR);
    CHECK(vfs_chdir("/") == MS_OK);
}

static void test_remove_and_blocks(int free_at_start)
{
    CHECK(vfs_remove("/projects") == MS_ERR);             /* not empty */
    CHECK(strcmp(vfs_last_error(), "Directory not empty") == 0);
    CHECK(vfs_remove("/projects/a.txt") == MS_OK);
    CHECK(vfs_remove("/projects/new.txt") == MS_OK);
    CHECK(vfs_remove("/projects/sub") == MS_OK);
    CHECK(vfs_remove("/projects") == MS_OK);
    CHECK(vfs_lookup("/projects") == MS_ERR);
    CHECK(vfs_remove("/") == MS_ERR);
    CHECK(vfs_remove("/nope") == MS_ERR);

    /* every block must have been given back to the bitmap */
    CHECK(vfs_free_block_count() == free_at_start);

    /* the biggest allowed file, then one byte too many */
    static char big[FS_MAX_FILE_SIZE];
    memset(big, 'x', sizeof big);
    CHECK(vfs_write("/big.txt", big, FS_MAX_FILE_SIZE, 0) == MS_OK);
    CHECK(read_str("/big.txt") == FS_MAX_FILE_SIZE);
    CHECK(vfs_free_block_count() == free_at_start - FS_DIRECT_BLOCKS);
    CHECK(vfs_write("/big.txt", "y", 1, 1) == MS_ERR);
    CHECK(read_str("/big.txt") == FS_MAX_FILE_SIZE);      /* unchanged */
    CHECK(vfs_remove("/big.txt") == MS_OK);
    CHECK(vfs_free_block_count() == free_at_start);
}

static void test_save_load(void)
{
    char path[] = "/tmp/minishell_disk_XXXXXX";
    close(mkstemp(path));

    CHECK(vfs_write("/tmp/keep.txt", "saved", 5, 0) == MS_OK);
    CHECK(vfs_save(path) == MS_OK);
    vfs_init();                                           /* wipe the disk */
    CHECK(vfs_lookup("/tmp/keep.txt") == MS_ERR);
    CHECK(vfs_load(path) == MS_OK);                       /* bring it back */
    CHECK(read_str("/tmp/keep.txt") == 5 && strcmp(buf, "saved") == 0);

    /* a file that is not a disk is rejected */
    FILE *f = fopen(path, "wb");
    fputs("garbage", f);
    fclose(f);
    CHECK(vfs_load(path) == MS_ERR);
    CHECK(read_str("/tmp/keep.txt") == 5);                /* disk untouched */
    unlink(path);
}

static void test_permissions(void)
{
    /* alice makes a file in her home and allows only her group to read it */
    CHECK(users_login("alice", "alice") == MS_OK);
    CHECK(vfs_write("/home/alice/notes.txt", "secret", 6, 0) == MS_OK);
    const Inode *n = vfs_get_inode(vfs_lookup("/home/alice/notes.txt"));
    CHECK(n != NULL && n->owner_uid == 1000 && n->mode == 0644);
    CHECK(vfs_chmod("/home/alice/notes.txt", 0640) == MS_OK);
    CHECK(vfs_mkdir("/docs") == MS_ERR);                  /* "/" belongs to root */
    CHECK(strcmp(vfs_last_error(), "Permission denied") == 0);
    CHECK(vfs_write("/tmp/shared.txt", "hi", 2, 0) == MS_OK);   /* /tmp is 777 */

    /* bob is in the same group (users): may read, may not write */
    CHECK(users_login("bob", "bob") == MS_OK);
    CHECK(read_str("/home/alice/notes.txt") == 6);
    CHECK(vfs_write("/home/alice/notes.txt", "x", 1, 0) == MS_ERR);
    CHECK(vfs_create("/home/alice/bob.txt") == MS_ERR);   /* alice's dir is 755 */
    CHECK(vfs_chmod("/home/alice/notes.txt", 0777) == MS_ERR);  /* not the owner */

    /* carol is in another group: may not even read */
    CHECK(users_login("carol", "carol") == MS_OK);
    CHECK(vfs_read("/home/alice/notes.txt", buf, 10) == MS_ERR);

    /* alice removes 'x' from her home: nobody else can cd into it */
    CHECK(users_login("alice", "alice") == MS_OK);
    CHECK(vfs_chmod("/home/alice", 0700) == MS_OK);
    CHECK(users_login("bob", "bob") == MS_OK);
    CHECK(vfs_chdir("/home/alice") == MS_ERR);

    /* root may do anything */
    CHECK(users_login("root", "root") == MS_OK);
    CHECK(read_str("/home/alice/notes.txt") == 6);
    CHECK(vfs_chdir("/home/alice") == MS_OK);
    CHECK(vfs_chdir("/") == MS_OK);
}

void test_fs(void)
{
    users_init();                    /* root, so the tests may create anything */
    CHECK(vfs_init() == MS_OK);
    int free_at_start = vfs_free_block_count();

    test_geometry();
    test_basic();
    test_cwd();
    test_remove_and_blocks(free_at_start);
    test_save_load();
    test_permissions();

    users_init();                    /* leave things as we found them */
    vfs_init();
}
