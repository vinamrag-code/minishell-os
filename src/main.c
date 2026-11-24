/*
 * main.c - Program entry point: start every module, then run the prompt loop.
 *
 *   ./minishell              uses the virtual disk file "minishell.disk"
 *   ./minishell other.disk   uses another disk file
 *
 * The loop is the heart of any shell:
 *     1. print a prompt
 *     2. read a line
 *     3. parse it into commands
 *     4. run them
 *     5. repeat until 'exit' or end of input (Ctrl+D)
 * On the way out, the virtual disk is saved.
 */
#include <stdio.h>
#include "common.h"
#include "shell.h"
#include "fs.h"
#include "proc.h"
#include "mem.h"
#include "users.h"

int main(int argc, char **argv)
{
    const char *disk_file = (argc > 1) ? argv[1] : "minishell.disk";

    /* Order matters: memory first (the parser uses it), then users
     * (new files need an owner), then the disk, then signals. */
    mem_init();
    users_init();

    FILE *f = fopen(disk_file, "rb");
    if (f != NULL) {
        fclose(f);
        if (vfs_load(disk_file) != MS_OK) {
            fprintf(stderr, "minishell: cannot load %s (%s), starting a fresh disk\n",
                    disk_file, vfs_last_error());
            vfs_init();
        }
    } else {
        vfs_init();                       /* first run: format a new disk */
    }

    signals_init();

    int interactive = proc_interactive();
    if (interactive)
        printf("Welcome to MiniShell OS. Type 'help' to see the commands.\n");

    char line[MAX_LINE];

    while (!shell_should_exit()) {
        jobs_reap(1);                     /* report finished background jobs */

        if (interactive) {                /* no prompt when reading a script */
            char cwd[MAX_PATH];
            if (vfs_getcwd(cwd, sizeof cwd) != MS_OK)
                cwd[0] = '\0';
            printf("%s@minishell:%s$ ", current_user()->name, cwd);
            fflush(stdout);
        }

        signals_take_interrupt();         /* forget any old Ctrl+C */
        if (fgets(line, sizeof line, stdin) == NULL) {
            if (signals_take_interrupt()) {   /* Ctrl+C: just a new prompt */
                clearerr(stdin);
                continue;
            }
            if (interactive)
                printf("\n");                 /* Ctrl+D */
            break;
        }

        Pipeline p;
        if (parse_line(line, &p) == MS_OK)
            execute_pipeline(&p);
        free_pipeline(&p);
    }

    jobs_kill_all();
    if (vfs_save(disk_file) != MS_OK)
        fprintf(stderr, "minishell: could not save %s: %s\n", disk_file, vfs_last_error());
    return 0;
}
