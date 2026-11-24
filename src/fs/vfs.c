/*
 * vfs.c - The virtual file system (Person 2).
 *
 * Everything lives in one struct, 'disk'. Saving the disk is just
 * writing that struct to a real file.
 *
 * Layers, from bottom to top:
 *   1. blocks + bitmap       which 512-byte blocks are free
 *   2. inodes                one record per file/directory
 *   3. inode_read/write_at   read/write bytes of a file at any offset
 *   4. directories           lists of DirEntry stored as file data
 *   5. paths                 "/home/alice/notes.txt" -> inode number
 *   6. vfs_* functions       what other modules call
 *   7. cmd_* functions       what the user types
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "fs.h"
#include "users.h"

#define FS_MAGIC 0x534F534DU   /* the letters "MSOS": marks a valid disk file */

typedef struct {
    unsigned int  magic;
    Inode         inodes[FS_MAX_INODES];
    unsigned char bitmap[FS_NUM_BLOCKS / 8];          /* 1 bit per block */
    unsigned char blocks[FS_NUM_BLOCKS][FS_BLOCK_SIZE];
} Disk;

static Disk disk;
static int  cwd = FS_ROOT_INODE;      /* inode of the current directory */
static char last_error[128] = "";

/* Records why something failed and returns MS_ERR. */
static int fail(const char *msg)
{
    snprintf(last_error, sizeof last_error, "%s", msg);
    return MS_ERR;
}

const char *vfs_last_error(void)
{
    return last_error;
}

/* =================== 1. Blocks and the free-block bitmap ================
 * Block b is bit (b % 8) of byte (b / 8).  1 = used, 0 = free. */

static int block_is_used(int b)
{
    return (disk.bitmap[b / 8] >> (b % 8)) & 1;
}

static void block_set_used(int b, int used)
{
    if (used)
        disk.bitmap[b / 8] |= (unsigned char)(1u << (b % 8));
    else
        disk.bitmap[b / 8] &= (unsigned char)~(1u << (b % 8));
}

/* Finds a free block, marks it used and zeroes it. */
static int block_alloc(void)
{
    for (int b = 0; b < FS_NUM_BLOCKS; b++) {
        if (!block_is_used(b)) {
            block_set_used(b, 1);
            memset(disk.blocks[b], 0, FS_BLOCK_SIZE);
            return b;
        }
    }
    return MS_ERR;
}

int vfs_free_block_count(void)
{
    int count = 0;
    for (int b = 0; b < FS_NUM_BLOCKS; b++)
        if (!block_is_used(b))
            count++;
    return count;
}

/* =========================== 2. Inodes ================================== */

static Inode *inode(int ino)
{
    return &disk.inodes[ino];
}

/* Finds a free inode and sets it up, owned by the current user. */
static int inode_alloc(InodeType type, int mode)
{
    for (int i = 0; i < FS_MAX_INODES; i++) {
        if (disk.inodes[i].type == INODE_FREE) {
            Inode *n = &disk.inodes[i];
            const User *u = current_user();
            n->type = type;
            n->size = 0;
            n->owner_uid = u ? u->uid : ROOT_UID;
            n->group_gid = u ? u->gid : 0;
            n->mode = mode;
            for (int k = 0; k < FS_DIRECT_BLOCKS; k++)
                n->blocks[k] = -1;
            return i;
        }
    }
    return fail("No free inodes (too many files)");
}

/* Gives back all data blocks of a file and makes it empty. */
static void inode_truncate(int ino)
{
    Inode *n = inode(ino);
    for (int k = 0; k < FS_DIRECT_BLOCKS; k++) {
        if (n->blocks[k] >= 0)
            block_set_used(n->blocks[k], 0);
        n->blocks[k] = -1;
    }
    n->size = 0;
}

static void inode_free(int ino)
{
    inode_truncate(ino);
    inode(ino)->type = INODE_FREE;
}

/* Small permission helper: may the current user do 'wanted' on this inode? */
static int can(int ino, int wanted)
{
    Inode *n = inode(ino);
    return perm_check(n->owner_uid, n->group_gid, n->mode, wanted);
}

/* ================ 3. Reading and writing bytes of a file ================
 * Byte number 'pos' of a file is in block  blocks[pos / 512]
 * at position  pos % 512  inside that block. */

static int inode_read_at(int ino, int offset, char *buf, int len)
{
    Inode *n = inode(ino);
    if (offset >= n->size)
        return 0;
    if (offset + len > n->size)
        len = n->size - offset;

    int done = 0;
    while (done < len) {
        int pos   = offset + done;
        int bi    = pos / FS_BLOCK_SIZE;      /* which of the inode's blocks */
        int bo    = pos % FS_BLOCK_SIZE;      /* where inside that block     */
        int chunk = FS_BLOCK_SIZE - bo;
        if (chunk > len - done)
            chunk = len - done;
        memcpy(buf + done, disk.blocks[n->blocks[bi]] + bo, chunk);
        done += chunk;
    }
    return done;
}

static int inode_write_at(int ino, int offset, const char *data, int len)
{
    Inode *n = inode(ino);
    if (len == 0)
        return 0;
    if (offset + len > FS_MAX_FILE_SIZE)
        return fail("File too large (max 6 KB)");

    /* Check there are enough free blocks BEFORE changing anything,
     * so a full disk never leaves a half-written file. */
    int needed = 0;
    for (int bi = offset / FS_BLOCK_SIZE; bi <= (offset + len - 1) / FS_BLOCK_SIZE; bi++)
        if (n->blocks[bi] < 0)
            needed++;
    if (needed > vfs_free_block_count())
        return fail("Disk full");

    int done = 0;
    while (done < len) {
        int pos   = offset + done;
        int bi    = pos / FS_BLOCK_SIZE;
        int bo    = pos % FS_BLOCK_SIZE;
        int chunk = FS_BLOCK_SIZE - bo;
        if (chunk > len - done)
            chunk = len - done;
        if (n->blocks[bi] < 0)
            n->blocks[bi] = block_alloc();
        memcpy(disk.blocks[n->blocks[bi]] + bo, data + done, chunk);
        done += chunk;
    }
    if (offset + len > n->size)
        n->size = offset + len;
    return done;
}

/* ========================== 4. Directories ============================== */

static int dir_entry_count(int dir)
{
    return inode(dir)->size / (int)sizeof(DirEntry);
}

static void dir_get(int dir, int i, DirEntry *e)
{
    inode_read_at(dir, i * (int)sizeof(DirEntry), (char *)e, sizeof *e);
}

/* Returns the inode number for 'name' inside 'dir', or MS_ERR. */
static int dir_find(int dir, const char *name)
{
    DirEntry e;
    for (int i = 0; i < dir_entry_count(dir); i++) {
        dir_get(dir, i, &e);
        if (e.inode >= 0 && strcmp(e.name, name) == 0)
            return e.inode;
    }
    return MS_ERR;
}

/* Adds "name -> ino" to 'dir', reusing an empty slot if there is one. */
static int dir_add(int dir, const char *name, int ino)
{
    DirEntry e;
    int slot = dir_entry_count(dir);       /* default: add at the end */
    for (int i = 0; i < dir_entry_count(dir); i++) {
        dir_get(dir, i, &e);
        if (e.inode < 0) {
            slot = i;
            break;
        }
    }
    memset(&e, 0, sizeof e);
    strncpy(e.name, name, FS_NAME_LEN - 1);
    e.inode = ino;
    if (inode_write_at(dir, slot * (int)sizeof(DirEntry), (char *)&e, sizeof e) < 0)
        return MS_ERR;
    return MS_OK;
}

static void dir_remove_entry(int dir, const char *name)
{
    DirEntry e;
    for (int i = 0; i < dir_entry_count(dir); i++) {
        dir_get(dir, i, &e);
        if (e.inode >= 0 && strcmp(e.name, name) == 0) {
            e.inode = -1;                  /* mark the slot empty */
            inode_write_at(dir, i * (int)sizeof(DirEntry), (char *)&e, sizeof e);
            return;
        }
    }
}

static int is_dot_name(const char *name)
{
    return strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
}

/* Empty = nothing except "." and "..". */
static int dir_is_empty(int dir)
{
    DirEntry e;
    for (int i = 0; i < dir_entry_count(dir); i++) {
        dir_get(dir, i, &e);
        if (e.inode >= 0 && !is_dot_name(e.name))
            return 0;
    }
    return 1;
}

/* Creates a new file or directory called 'name' inside 'parent'. */
static int make_node(int parent, const char *name, InodeType type, int mode)
{
    if (dir_find(parent, name) >= 0)
        return fail("File exists");

    int ino = inode_alloc(type, mode);
    if (ino < 0)
        return MS_ERR;

    if (type == INODE_DIR) {
        if (dir_add(ino, ".", ino) != MS_OK || dir_add(ino, "..", parent) != MS_OK) {
            inode_free(ino);
            return MS_ERR;
        }
    }
    if (dir_add(parent, name, ino) != MS_OK) {
        inode_free(ino);
        return MS_ERR;
    }
    return ino;
}

/* ============================ 5. Paths ================================== */

/* Follows a path one name at a time and returns the final inode. */
static int resolve(const char *path)
{
    if (path == NULL || path[0] == '\0')
        return fail("No such file or directory");
    if (strlen(path) >= MAX_PATH)
        return fail("Path too long");

    char copy[MAX_PATH];
    strcpy(copy, path);

    int cur = (path[0] == '/') ? FS_ROOT_INODE : cwd;
    char *save = NULL;
    for (char *part = strtok_r(copy, "/", &save); part != NULL;
         part = strtok_r(NULL, "/", &save)) {
        if (inode(cur)->type != INODE_DIR)
            return fail("Not a directory");
        int next = dir_find(cur, part);
        if (next < 0)
            return fail("No such file or directory");
        cur = next;
    }
    return cur;
}

/* Splits "/a/b/c" into the parent directory (inode of /a/b) and "c". */
static int resolve_parent(const char *path, int *parent, char *name)
{
    if (path == NULL || path[0] == '\0')
        return fail("No such file or directory");
    if (strlen(path) >= MAX_PATH)
        return fail("Path too long");

    char copy[MAX_PATH];
    strcpy(copy, path);
    int len = (int)strlen(copy);
    while (len > 1 && copy[len - 1] == '/')   /* "docs/" -> "docs" */
        copy[--len] = '\0';

    char *slash = strrchr(copy, '/');
    const char *base;
    if (slash == NULL) {                      /* "notes.txt" */
        *parent = cwd;
        base = copy;
    } else {
        base = slash + 1;
        if (slash == copy) {                  /* "/notes.txt" */
            *parent = FS_ROOT_INODE;
        } else {                              /* "a/b/notes.txt" */
            *slash = '\0';
            *parent = resolve(copy);
            if (*parent < 0)
                return MS_ERR;
        }
    }

    if (inode(*parent)->type != INODE_DIR)
        return fail("Not a directory");
    if (base[0] == '\0' || is_dot_name(base))
        return fail("Invalid name");
    if (strlen(base) >= FS_NAME_LEN)
        return fail("Name too long (max 27 characters)");
    strcpy(name, base);
    return MS_OK;
}

/* ===================== 6. Functions other modules call ================== */

int vfs_init(void)
{
    memset(&disk, 0, sizeof disk);
    disk.magic = FS_MAGIC;

    /* The root directory: its ".." points to itself. */
    int root = inode_alloc(INODE_DIR, 0755);
    dir_add(root, ".", root);
    dir_add(root, "..", root);
    cwd = root;

    int home = make_node(root, "home", INODE_DIR, 0755);
    int tmp  = make_node(root, "tmp",  INODE_DIR, 0777);
    if (home < 0 || tmp < 0)
        return MS_ERR;

    /* One home directory per normal user, owned by that user. */
    for (int i = 0; i < users_count(); i++) {
        const User *u = users_get(i);
        if (u->uid == ROOT_UID)
            continue;
        int h = make_node(home, u->name, INODE_DIR, 0755);
        if (h < 0)
            return MS_ERR;
        inode(h)->owner_uid = u->uid;
        inode(h)->group_gid = u->gid;
    }
    return MS_OK;
}

int vfs_save(const char *real_path)
{
    FILE *f = fopen(real_path, "wb");
    if (f == NULL)
        return fail(strerror(errno));
    size_t ok = fwrite(&disk, sizeof disk, 1, f);
    fclose(f);
    return ok == 1 ? MS_OK : fail("write error");
}

int vfs_load(const char *real_path)
{
    static Disk loaded;                  /* read here first, check, then copy */
    FILE *f = fopen(real_path, "rb");
    if (f == NULL)
        return fail(strerror(errno));
    size_t ok = fread(&loaded, sizeof loaded, 1, f);
    fclose(f);
    if (ok != 1 || loaded.magic != FS_MAGIC)
        return fail("not a MiniShell disk file");
    disk = loaded;
    cwd = FS_ROOT_INODE;
    return MS_OK;
}

int vfs_lookup(const char *path)
{
    return resolve(path);
}

const Inode *vfs_get_inode(int inode_num)
{
    if (inode_num < 0 || inode_num >= FS_MAX_INODES ||
        disk.inodes[inode_num].type == INODE_FREE)
        return NULL;
    return &disk.inodes[inode_num];
}

static int create_node(const char *path, InodeType type, int mode)
{
    int parent;
    char name[FS_NAME_LEN];
    if (resolve_parent(path, &parent, name) != MS_OK)
        return MS_ERR;
    if (!can(parent, PERM_WRITE))
        return fail("Permission denied");
    int ino = make_node(parent, name, type, mode);
    return ino < 0 ? MS_ERR : MS_OK;
}

int vfs_mkdir(const char *path)
{
    return create_node(path, INODE_DIR, 0755);
}

int vfs_create(const char *path)
{
    return create_node(path, INODE_FILE, 0644);
}

/* Is 'dir' the current directory or one of its parents? */
static int dir_in_use(int dir)
{
    int cur = cwd;
    while (1) {
        if (cur == dir)
            return 1;
        if (cur == FS_ROOT_INODE)
            return 0;
        cur = dir_find(cur, "..");
    }
}

int vfs_remove(const char *path)
{
    int parent;
    char name[FS_NAME_LEN];
    if (resolve_parent(path, &parent, name) != MS_OK)
        return MS_ERR;

    int ino = dir_find(parent, name);
    if (ino < 0)
        return fail("No such file or directory");
    if (!can(parent, PERM_WRITE))
        return fail("Permission denied");

    if (inode(ino)->type == INODE_DIR) {
        if (!dir_is_empty(ino))
            return fail("Directory not empty");
        if (dir_in_use(ino))
            return fail("Directory is in use (you are inside it)");
    }
    dir_remove_entry(parent, name);
    inode_free(ino);
    return MS_OK;
}

int vfs_read(const char *path, char *buf, int bufsize)
{
    int ino = resolve(path);
    if (ino < 0)
        return MS_ERR;
    if (inode(ino)->type == INODE_DIR)
        return fail("Is a directory");
    if (!can(ino, PERM_READ))
        return fail("Permission denied");
    return inode_read_at(ino, 0, buf, bufsize);
}

int vfs_write(const char *path, const char *data, int len, int append)
{
    int ino = resolve(path);
    if (ino < 0) {                        /* does not exist yet: create it */
        if (vfs_create(path) != MS_OK)
            return MS_ERR;
        ino = resolve(path);
    } else if (!can(ino, PERM_WRITE)) {
        return fail("Permission denied");
    }
    if (inode(ino)->type == INODE_DIR)
        return fail("Is a directory");

    int offset = append ? inode(ino)->size : 0;
    if (offset + len > FS_MAX_FILE_SIZE)  /* check before truncating */
        return fail("File too large (max 6 KB)");
    if (!append)
        inode_truncate(ino);
    return inode_write_at(ino, offset, data, len) < 0 ? MS_ERR : MS_OK;
}

int vfs_chdir(const char *path)
{
    int ino = resolve(path);
    if (ino < 0)
        return MS_ERR;
    if (inode(ino)->type != INODE_DIR)
        return fail("Not a directory");
    if (!can(ino, PERM_EXEC))             /* 'x' on a directory = may enter */
        return fail("Permission denied");
    cwd = ino;
    return MS_OK;
}

/* Builds the path by walking up with ".." and looking up each name. */
int vfs_getcwd(char *buf, int bufsize)
{
    char path[MAX_PATH] = "";
    int cur = cwd;

    while (cur != FS_ROOT_INODE) {
        int parent = dir_find(cur, "..");
        DirEntry e;
        int found = 0;
        for (int i = 0; i < dir_entry_count(parent); i++) {
            dir_get(parent, i, &e);
            if (e.inode == cur && !is_dot_name(e.name)) {
                found = 1;
                break;
            }
        }
        if (!found)
            return fail("Broken directory tree");

        /* Prepend "/name" to the path we have built so far. */
        size_t name_len = strlen(e.name), path_len = strlen(path);
        if (1 + name_len + path_len >= MAX_PATH)
            return fail("Path too long");
        memmove(path + 1 + name_len, path, path_len + 1);  /* shift right */
        path[0] = '/';
        memcpy(path + 1, e.name, name_len);
        cur = parent;
    }
    if (path[0] == '\0')
        strcpy(path, "/");

    if ((int)strlen(path) >= bufsize)
        return fail("Buffer too small");
    strcpy(buf, path);
    return MS_OK;
}

int vfs_chmod(const char *path, int mode)
{
    int ino = resolve(path);
    if (ino < 0)
        return MS_ERR;
    const User *u = current_user();
    if (u->uid != ROOT_UID && u->uid != inode(ino)->owner_uid)
        return fail("Operation not permitted (only the owner or root)");
    inode(ino)->mode = mode & 0777;
    return MS_OK;
}

/* ========================= 7. Built-in commands ========================= */

/* Prints e.g. "rm: notes.txt: No such file or directory" */
static int report(const char *cmd, const char *path)
{
    fprintf(stderr, "%s: %s: %s\n", cmd, path, vfs_last_error());
    return MS_ERR;
}

/* 0754 -> "rwxr-xr--" */
static void mode_string(int mode, char *out)
{
    const char *letters = "rwxrwxrwx";
    for (int i = 0; i < 9; i++)
        out[i] = (mode & (0400 >> i)) ? letters[i] : '-';
    out[9] = '\0';
}

static void print_entry(const char *name, int ino, int long_format)
{
    Inode *n = inode(ino);
    const char *suffix = (n->type == INODE_DIR) ? "/" : "";
    if (long_format) {
        char perms[10];
        mode_string(n->mode, perms);
        printf("%c%s  %-6s %-6s %5d  %s%s\n",
               n->type == INODE_DIR ? 'd' : '-', perms,
               users_name(n->owner_uid), users_group_name(n->group_gid),
               n->size, name, suffix);
    } else {
        printf("%s%s\n", name, suffix);
    }
}

int cmd_ls(int argc, char **argv)
{
    int long_format = 0;
    const char *path = ".";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0)
            long_format = 1;
        else
            path = argv[i];
    }

    int ino = resolve(path);
    if (ino < 0)
        return report("ls", path);

    if (inode(ino)->type == INODE_FILE) {
        print_entry(path, ino, long_format);
        return MS_OK;
    }
    if (!can(ino, PERM_READ)) {
        fail("Permission denied");
        return report("ls", path);
    }

    /* Collect the names, then sort them alphabetically (insertion sort). */
    DirEntry list[FS_MAX_INODES];
    int count = 0;
    DirEntry e;
    for (int i = 0; i < dir_entry_count(ino) && count < FS_MAX_INODES; i++) {
        dir_get(ino, i, &e);
        if (e.inode >= 0 && !is_dot_name(e.name))
            list[count++] = e;
    }
    for (int i = 1; i < count; i++) {
        DirEntry key = list[i];
        int j = i - 1;
        while (j >= 0 && strcmp(list[j].name, key.name) > 0) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = key;
    }

    for (int i = 0; i < count; i++)
        print_entry(list[i].name, list[i].inode, long_format);
    return MS_OK;
}

int cmd_cd(int argc, char **argv)
{
    char home[MAX_PATH];
    const char *path = argv[1];
    if (argc < 2) {                       /* plain "cd" goes home */
        const User *u = current_user();
        if (u->uid == ROOT_UID)
            strcpy(home, "/");
        else
            snprintf(home, sizeof home, "/home/%s", u->name);
        path = home;
    }
    if (vfs_chdir(path) != MS_OK)
        return report("cd", path);
    return MS_OK;
}

int cmd_pwd(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    char buf[MAX_PATH];
    if (vfs_getcwd(buf, sizeof buf) != MS_OK)
        return report("pwd", ".");
    printf("%s\n", buf);
    return MS_OK;
}

int cmd_mkdir(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: mkdir path...\n");
        return MS_ERR;
    }
    int rc = MS_OK;
    for (int i = 1; i < argc; i++)
        if (vfs_mkdir(argv[i]) != MS_OK)
            rc = report("mkdir", argv[i]);
    return rc;
}

int cmd_touch(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: touch path...\n");
        return MS_ERR;
    }
    int rc = MS_OK;
    for (int i = 1; i < argc; i++) {
        if (resolve(argv[i]) >= 0)
            continue;                     /* already exists: nothing to do */
        if (vfs_create(argv[i]) != MS_OK)
            rc = report("touch", argv[i]);
    }
    return rc;
}

int cmd_cat(int argc, char **argv)
{
    static char buf[FS_MAX_FILE_SIZE];

    if (argc < 2) {                       /* no file: copy stdin to stdout */
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, stdin)) > 0)
            fwrite(buf, 1, n, stdout);
        return MS_OK;
    }

    int rc = MS_OK;
    for (int i = 1; i < argc; i++) {
        int n = vfs_read(argv[i], buf, sizeof buf);
        if (n < 0)
            rc = report("cat", argv[i]);
        else
            fwrite(buf, 1, n, stdout);
    }
    return rc;
}

int cmd_rm(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: rm path...\n");
        return MS_ERR;
    }
    int rc = MS_OK;
    for (int i = 1; i < argc; i++)
        if (vfs_remove(argv[i]) != MS_OK)
            rc = report("rm", argv[i]);
    return rc;
}

/* write [-a] path text...   joins the words with spaces and adds a newline.
 * write [-a] path           (no text) reads the text from stdin until Ctrl+D. */
int cmd_write(int argc, char **argv)
{
    static char buf[FS_MAX_FILE_SIZE + 1];
    int i = 1, append = 0;
    if (i < argc && strcmp(argv[i], "-a") == 0) {
        append = 1;
        i++;
    }
    if (i >= argc) {
        fprintf(stderr, "usage: write [-a] path [text...]\n");
        return MS_ERR;
    }
    const char *path = argv[i++];

    int len = 0;
    if (i < argc) {
        for (; i < argc; i++) {
            int n = snprintf(buf + len, sizeof buf - len, "%s%s",
                             argv[i], i < argc - 1 ? " " : "\n");
            if (n < 0 || len + n >= (int)sizeof buf) {
                fprintf(stderr, "write: text too long\n");
                return MS_ERR;
            }
            len += n;
        }
    } else {
        len = (int)fread(buf, 1, sizeof buf, stdin);
    }

    if (vfs_write(path, buf, len, append) != MS_OK)
        return report("write", path);
    return MS_OK;
}
