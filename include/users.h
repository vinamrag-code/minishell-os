/*
 * users.h - Users and permissions interface (Person 4).
 *
 * Every file has an owner, a group, and 9 permission bits in three sets:
 *
 *        owner   group   other
 *        r w x   r w x   r w x      e.g. 0754 = rwx r-x r--
 *        4 2 1   4 2 1   4 2 1
 *
 * perm_check() takes plain numbers instead of an Inode, so this module
 * does not depend on the file system. The file system calls it before
 * reading or writing.
 *
 * Built-in users (name / password / group):
 *   root  / root  / root    (may do anything)
 *   alice / alice / users
 *   bob   / bob   / users
 *   carol / carol / staff
 *
 * Note: this is a simulation. Passwords are stored as plain text,
 * which a real system must never do.
 */
#ifndef USERS_H
#define USERS_H

#include "common.h"

#define PERM_READ   4
#define PERM_WRITE  2
#define PERM_EXEC   1

#define ROOT_UID    0   /* root may do anything */

typedef struct {
    int  uid;          /* user ID  */
    int  gid;          /* group ID */
    char name[32];
    char password[32];
} User;

/* Sets up the built-in users and logs in as root. */
int users_init(void);

/* Switches to another user if the password matches.
 * Returns MS_OK, or MS_ERR for an unknown user or wrong password. */
int users_login(const char *name, const char *password);

/* The user who is logged in right now (never NULL after users_init). */
const User *current_user(void);

int users_count(void);                  /* number of built-in users     */
const User *users_get(int index);       /* user number 'index', or NULL */
const char *users_name(int uid);        /* "alice", or "?" if unknown   */
const char *users_group_name(int gid);  /* "users", or "?" if unknown   */

/* May the current user do 'wanted' (PERM_READ / WRITE / EXEC, or several
 * OR-ed together) on a file with this owner, group and mode?
 * Returns 1 = allowed, 0 = denied. */
int perm_check(int owner_uid, int group_gid, int mode, int wanted);

/* Built-in commands */
int cmd_login(int argc, char **argv);   /* login <name> [password] */
int cmd_whoami(int argc, char **argv);  /* whoami                  */
int cmd_chmod(int argc, char **argv);   /* chmod 644 <path>...     */

#endif /* USERS_H */
