/*
 * parser.c - Turns a line of text into a Pipeline (Person 1).
 *
 * We walk through the line one character at a time:
 *   - spaces separate words
 *   - "..." and '...' keep spaces inside one word:  "hello world"
 *   - |  starts the next command in the pipeline
 *   - <  >  >>  take the next word as a file name
 *   - &  (only at the end) runs the whole line in the background
 *
 * Every word is copied into memory from OUR allocator (mem_alloc),
 * and free_pipeline() gives it back with mem_free.
 */
#include <string.h>
#include "shell.h"
#include "mem.h"

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int is_operator(char c)
{
    return c == '|' || c == '<' || c == '>' || c == '&';
}

static int syntax_error(const char *msg)
{
    fprintf(stderr, "minishell: syntax error: %s\n", msg);
    return MS_ERR;
}

/* Copies a string into our memory pool. Returns NULL if the pool is full. */
static char *copy_to_pool(const char *s)
{
    size_t n = strlen(s) + 1;          /* +1 for the '\0' */
    char *p = mem_alloc(n);
    if (p != NULL)
        memcpy(p, s, n);
    return p;
}

/* Reads one word starting at *s into 'word' and moves *s past it.
 * Quotes are removed:  he"llo wor"ld  ->  hello world
 * Returns MS_ERR if a quote is never closed. */
static int read_word(const char **s, char *word, int size)
{
    const char *p = *s;
    int len = 0;

    while (*p != '\0' && !is_space(*p) && !is_operator(*p)) {
        if (*p == '"' || *p == '\'') {
            char quote = *p++;                 /* remember which quote */
            while (*p != '\0' && *p != quote) {
                if (len < size - 1)
                    word[len++] = *p;
                p++;
            }
            if (*p != quote)
                return MS_ERR;                 /* reached the end: no closing quote */
            p++;                               /* skip the closing quote */
        } else {
            if (len < size - 1)
                word[len++] = *p;
            p++;
        }
    }
    word[len] = '\0';
    *s = p;
    return MS_OK;
}

int parse_line(const char *line, Pipeline *out)
{
    memset(out, 0, sizeof *out);   /* every pointer starts as NULL */

    /* Keep a copy of the line (without the trailing newline) for 'jobs'. */
    strncpy(out->cmdline, line, MAX_LINE - 1);
    size_t n = strlen(out->cmdline);
    while (n > 0 && is_space(out->cmdline[n - 1]))
        out->cmdline[--n] = '\0';

    char word[MAX_LINE];
    int idx = 0;                   /* which command we are filling in */
    const char *s = line;

    while (1) {
        while (is_space(*s))
            s++;
        if (*s == '\0')
            break;

        Command *cmd = &out->cmds[idx];

        if (out->background)
            return syntax_error("'&' must be at the end of the line");

        if (*s == '|') {
            if (cmd->argc == 0)
                return syntax_error("missing command before '|'");
            idx++;
            if (idx >= MAX_PIPE_CMDS)
                return syntax_error("too many commands in the pipeline");
            s++;
            continue;
        }

        if (*s == '&') {
            if (cmd->argc == 0)
                return syntax_error("missing command before '&'");
            out->background = 1;
            s++;
            continue;
        }

        if (*s == '<' || *s == '>') {
            char op = *s++;
            int append = 0;
            if (op == '>' && *s == '>') {      /* '>>' means append */
                append = 1;
                s++;
            }
            while (is_space(*s))
                s++;
            if (*s == '\0' || is_operator(*s))
                return syntax_error("missing file name after redirection");
            if (read_word(&s, word, sizeof word) != MS_OK)
                return syntax_error("missing closing quote");

            char *name = copy_to_pool(word);
            if (name == NULL)
                return syntax_error("out of memory");

            char **slot = (op == '<') ? &cmd->infile : &cmd->outfile;
            mem_free(*slot);                   /* "a > x > y": the last one wins */
            *slot = name;
            if (op == '>')
                cmd->append = append;
            continue;
        }

        /* Anything else is an ordinary word. */
        if (read_word(&s, word, sizeof word) != MS_OK)
            return syntax_error("missing closing quote");
        if (cmd->argc >= MAX_ARGS)
            return syntax_error("too many arguments");

        char *w = copy_to_pool(word);
        if (w == NULL)
            return syntax_error("out of memory");
        cmd->argv[cmd->argc++] = w;            /* argv[argc] stays NULL */
    }

    Command *last = &out->cmds[idx];

    /* A completely empty line is fine: nothing to run. */
    if (idx == 0 && last->argc == 0 && !last->infile && !last->outfile)
        return MS_OK;

    if (last->argc == 0)
        return syntax_error(idx > 0 ? "missing command after '|'"
                                    : "missing command");

    out->ncmds = idx + 1;
    return MS_OK;
}

void free_pipeline(Pipeline *p)
{
    /* Check every slot, not just ncmds: after a syntax error, words may
     * have been stored in commands that were never counted. */
    for (int i = 0; i < MAX_PIPE_CMDS; i++) {
        Command *c = &p->cmds[i];
        for (int j = 0; j < c->argc; j++) {
            mem_free(c->argv[j]);
            c->argv[j] = NULL;
        }
        c->argc = 0;
        mem_free(c->infile);
        mem_free(c->outfile);
        c->infile = c->outfile = NULL;
    }
    p->ncmds = 0;
}
