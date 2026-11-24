/*
 * users.c - Users, login and permission checks (Person 4).
 *
 * Permission check, step by step (like a real Linux kernel):
 *   1. root may do anything.
 *   2. If you OWN the file, use the owner bits  (rwx------).
 *   3. Else if you are in the file's GROUP, use the group bits (---rwx---).
 *   4. Else use the "other" bits (------rwx).
 * Only ONE set is used: an owner with r-- cannot write, even if
 * "other" has rw-.
 */
#include <stdlib.h>
#include <string.h>
#include "users.h"
#include "fs.h"

static const User users[] = {
    { 0,    0,   "root",  "root"  },
    { 1000, 100, "alice", "alice" },
    { 1001, 100, "bob",   "bob"   },
    { 1002, 200, "carol", "carol" },
};
#define NUM_USERS ((int)(sizeof users / sizeof users[0]))

static const struct { int gid; const char *name; } groups[] = {
    { 0,   "root"  },
    { 100, "users" },
    { 200, "staff" },
};
#define NUM_GROUPS ((int)(sizeof groups / sizeof groups[0]))

static const User *current = NULL;

int users_init(void)
{
    current = &users[0];                 /* start as root */
    return MS_OK;
}

int users_login(const char *name, const char *password)
{
    for (int i = 0; i < NUM_USERS; i++) {
        if (strcmp(users[i].name, name) == 0) {
            if (strcmp(users[i].password, password) != 0)
                return MS_ERR;
            current = &users[i];
            return MS_OK;
        }
    }
    return MS_ERR;
}

const User *current_user(void)
{
    return current;
}

int users_count(void)
{
    return NUM_USERS;
}

const User *users_get(int index)
{
    if (index < 0 || index >= NUM_USERS)
        return NULL;
    return &users[index];
}

const char *users_name(int uid)
{
    for (int i = 0; i < NUM_USERS; i++)
        if (users[i].uid == uid)
            return users[i].name;
    return "?";
}

const char *users_group_name(int gid)
{
    for (int i = 0; i < NUM_GROUPS; i++)
        if (groups[i].gid == gid)
            return groups[i].name;
    return "?";
}

int perm_check(int owner_uid, int group_gid, int mode, int wanted)
{
    const User *u = current;
    if (u == NULL)
        return 0;
    if (u->uid == ROOT_UID)
        return 1;

    int bits;
    if (u->uid == owner_uid)
        bits = (mode >> 6) & 7;          /* owner bits */
    else if (u->gid == group_gid)
        bits = (mode >> 3) & 7;          /* group bits */
    else
        bits = mode & 7;                 /* other bits */

    return (bits & wanted) == wanted;
}

/* ======================= Built-in commands ============================== */

int cmd_login(int argc, char **argv)
{
    char password[64];
    if (argc < 2) {
        fprintf(stderr, "usage: login <user> [password]\n");
        return MS_ERR;
    }
    if (argc >= 3) {
        snprintf(password, sizeof password, "%s", argv[2]);
    } else {
        printf("Password: ");
        fflush(stdout);
        if (fgets(password, sizeof password, stdin) == NULL)
            return MS_ERR;
        password[strcspn(password, "\n")] = '\0';
    }

    if (users_login(argv[1], password) != MS_OK) {
        fprintf(stderr, "login: wrong user name or password\n");
        return MS_ERR;
    }
    printf("Logged in as %s\n", current->name);

    /* Go to the new user's home directory (root's home is "/"). */
    char home[MAX_PATH];
    if (current->uid == ROOT_UID)
        strcpy(home, "/");
    else
        snprintf(home, sizeof home, "/home/%s", current->name);
    vfs_chdir(home);                     /* fine if it fails: stay where we are */
    return MS_OK;
}

int cmd_whoami(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    printf("%s\n", current->name);
    return MS_OK;
}

int cmd_chmod(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: chmod <mode> path...   e.g. chmod 640 notes.txt\n");
        return MS_ERR;
    }
    char *end;
    long mode = strtol(argv[1], &end, 8);    /* base 8 = octal */
    if (*end != '\0' || mode < 0 || mode > 0777) {
        fprintf(stderr, "chmod: %s: mode must be octal 000-777\n", argv[1]);
        return MS_ERR;
    }
    int rc = MS_OK;
    for (int i = 2; i < argc; i++) {
        if (vfs_chmod(argv[i], (int)mode) != MS_OK) {
            fprintf(stderr, "chmod: %s: %s\n", argv[i], vfs_last_error());
            rc = MS_ERR;
        }
    }
    return rc;
}
