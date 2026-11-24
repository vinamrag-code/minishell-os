/*
 * fs.h - Virtual file system interface (Person 2).
 *
 * The "disk" is a plain array of bytes in memory, split into fixed-size
 * BLOCKS. It is saved to one real file when the shell exits and loaded
 * again when it starts.
 *
 * What is on the disk:
 *   - an inode table: one Inode per file or directory
 *   - a free-block bitmap: one BIT per block, 1 = used, 0 = free
 *   - the data blocks: file contents and directory listings
 *
 * A directory is just a file whose data is a list of DirEntry records
 * (name -> inode number). Every directory has "." (itself) and ".." (parent).
 *
 * Starting layout made by vfs_init():
 *   /            root, rwxr-xr-x
 *   /home/alice  owned by alice (one home per user in users.h)
 *   /tmp         anyone may write, rwxrwxrwx
 */
#ifndef FS_H
#define FS_H

#include "common.h"

/* ---- Disk geometry ----------------------------------------------------- */
#define FS_BLOCK_SIZE     512    /* bytes per block                         */
#define FS_NUM_BLOCKS     1024   /* blocks on the disk: 1024 * 512 = 512 KB */
#define FS_MAX_INODES     128    /* most files + directories in total       */
#define FS_DIRECT_BLOCKS  12     /* blocks one inode can point to           */
#define FS_MAX_FILE_SIZE  (FS_DIRECT_BLOCKS * FS_BLOCK_SIZE)   /* 6 KB      */
#define FS_NAME_LEN       28     /* longest file name, including the '\0'   */
#define FS_ROOT_INODE     0      /* inode number of "/"                     */

/* ---- Inode ---------------------------------------------------------------
 * Describes one file or directory, like a library catalogue card:
 * what it is, how big it is, who owns it, and which blocks hold its data.
 * The file's NAME is not stored here. Names live in the parent directory. */
typedef enum {
    INODE_FREE = 0,   /* this slot is unused */
    INODE_FILE,
    INODE_DIR
} InodeType;

typedef struct {
    InodeType type;
    int size;                        /* bytes of data                      */
    int owner_uid;                   /* user who owns it (see users.h)     */
    int group_gid;                   /* group it belongs to                */
    int mode;                        /* permissions, e.g. 0644 = rw-r--r-- */
    int blocks[FS_DIRECT_BLOCKS];    /* block numbers; -1 means unused     */
} Inode;

/* One entry inside a directory: "this name means that inode".
 * 28 + 4 = 32 bytes, so exactly 16 entries fit in one block. */
typedef struct {
    char name[FS_NAME_LEN];
    int  inode;                      /* -1 means the entry is empty */
} DirEntry;

/* ---- Setup and saving -----------------------------------------------------
 * Call users_init() BEFORE vfs_init(), because new files are owned by the
 * current user. */

int vfs_init(void);                      /* format a fresh disk            */
int vfs_save(const char *real_path);     /* write the disk to a real file  */
int vfs_load(const char *real_path);     /* read it back                   */

/* ---- Core operations -------------------------------------------------------
 * Paths can be absolute ("/a/b") or relative to the current directory ("b"),
 * and may use "." and "..".
 * Each returns MS_OK (or a count), or MS_ERR on failure. After a failure,
 * vfs_last_error() says why, e.g. "No such file or directory".
 * Permission checks use the current user (users.h). */

int vfs_lookup(const char *path);          /* returns an inode number       */
const Inode *vfs_get_inode(int inode_num); /* read-only view, or NULL       */

int vfs_mkdir(const char *path);           /* new empty directory           */
int vfs_create(const char *path);          /* new empty file                */
int vfs_remove(const char *path);          /* delete a file or EMPTY dir    */

/* Copies up to 'bufsize' bytes of the file into 'buf' (no '\0' added).
 * Returns the number of bytes copied. */
int vfs_read(const char *path, char *buf, int bufsize);

/* Writes 'len' bytes of 'data' into the file, creating it if needed.
 * append = 1 adds to the end, append = 0 replaces the old contents. */
int vfs_write(const char *path, const char *data, int len, int append);

int vfs_chdir(const char *path);            /* change current directory  */
int vfs_getcwd(char *buf, int bufsize);     /* current directory as text */
int vfs_chmod(const char *path, int mode);  /* owner or root only        */

int vfs_free_block_count(void);             /* how many blocks are free  */
const char *vfs_last_error(void);           /* why the last call failed  */

/* ---- Built-in commands ------------------------------------------------------ */
int cmd_ls(int argc, char **argv);     /* ls [-l] [path]               */
int cmd_cd(int argc, char **argv);     /* cd [path]   (no path = home) */
int cmd_pwd(int argc, char **argv);    /* pwd                          */
int cmd_mkdir(int argc, char **argv);  /* mkdir path...                */
int cmd_touch(int argc, char **argv);  /* touch path...                */
int cmd_cat(int argc, char **argv);    /* cat [path...] (none = stdin) */
int cmd_rm(int argc, char **argv);     /* rm path...                   */
int cmd_write(int argc, char **argv);  /* write [-a] path [text...]    */

#endif /* FS_H */
