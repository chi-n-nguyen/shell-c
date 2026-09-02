#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shell.h"
#include "expand.h"

/* ------------------------------------------------------------------ */
/* Depth-aware pipe splitter                                           */
/* ------------------------------------------------------------------ */

/*
 * Split `line` on unquoted '|' characters, skipping any '|' that
 * appears inside a $(...) command substitution.  Writes pointers to
 * NUL-terminated segments into segs[] and returns the count, or -1 if
 * there are more than `max` segments.
 */
static int split_pipes(char *line, char **segs, int max) {
    int   n     = 0;
    int   depth = 0;   /* $( nesting depth */
    char *start = line;

    for (char *p = line; *p; p++) {
        if (*p == '$' && *(p + 1) == '(') { depth++; p++; continue; }
        if (depth > 0) {
            if      (*p == '(') depth++;
            else if (*p == ')') depth--;
            continue;
        }
        if (*p == '|') {
            if (n >= max) { fprintf(stderr, "shell-c: too many pipes\n"); return -1; }
            *p        = '\0';
            segs[n++] = start;
            start     = p + 1;
        }
    }
    if (n >= max) { fprintf(stderr, "shell-c: too many pipes\n"); return -1; }
    segs[n++] = start;
    return n;
}

/* ------------------------------------------------------------------ */
/* Depth-aware argument tokenizer                                      */
/* ------------------------------------------------------------------ */

/*
 * Return the next whitespace-delimited token from *pos, advancing *pos.
 * Whitespace inside $(...) is not treated as a delimiter, so
 * $(date +%Y) is returned as a single token.
 * Returns NULL when no more tokens remain.
 */
static char *next_arg(char **pos) {
    char *p = *pos;

    /* skip leading whitespace */
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) { *pos = p; return NULL; }

    char *start = p;
    int   depth = 0;

    while (*p) {
        if (*p == '$' && *(p + 1) == '(') { depth++; p += 2; continue; }
        if (depth > 0) {
            if      (*p == '(') depth++;
            else if (*p == ')') { if (--depth == 0) { p++; continue; } }
            p++;
            continue;
        }
        /* At depth 0, whitespace ends the token */
        if (*p == ' ' || *p == '\t') break;
        p++;
    }

    if (*p) { *p = '\0'; p++; }
    *pos = p;
    return start;
}

/* ------------------------------------------------------------------ */
/* Command parser                                                      */
/* ------------------------------------------------------------------ */

/*
 * Tokenise a single pipeline segment into a Command struct.
 * Redirection operators and '&' are recognised; all argument tokens
 * are passed through expand_token() for $VAR / $? / $() expansion.
 * Returns 0 on success, -1 on error.
 */
static int parse_command(char *segment, Command *cmd, int trace_mode) {
    memset(cmd, 0, sizeof(Command));

    char *pos = segment;
    char *token;

    while ((token = next_arg(&pos)) != NULL) {

        /* Input redirection */
        if (strcmp(token, "<") == 0) {
            char *file = next_arg(&pos);
            if (!file) { fprintf(stderr, "shell-c: expected filename after <\n"); return -1; }
            cmd->infile = file;

        /* Append redirection */
        } else if (strcmp(token, ">>") == 0) {
            char *file = next_arg(&pos);
            if (!file) { fprintf(stderr, "shell-c: expected filename after >>\n"); return -1; }
            cmd->outfile = file;
            cmd->append  = 1;

        /* Output redirection */
        } else if (strcmp(token, ">") == 0) {
            char *file = next_arg(&pos);
            if (!file) { fprintf(stderr, "shell-c: expected filename after >\n"); return -1; }
            cmd->outfile = file;
            cmd->append  = 0;

        /* Stderr → stdout: 2>&1 */
        } else if (strcmp(token, "2>&1") == 0) {
            cmd->err_to_out = 1;

        /* Stderr append: 2>>file  or  2>> file */
        } else if (strncmp(token, "2>>", 3) == 0) {
            char *file = token[3] ? token + 3 : next_arg(&pos);
            if (!file) { fprintf(stderr, "shell-c: expected filename after 2>>\n"); return -1; }
            cmd->errfile     = file;
            cmd->err_append  = 1;

        /* Stderr redirect: 2>file  or  2> file */
        } else if (strncmp(token, "2>", 2) == 0) {
            char *file = token[2] ? token + 2 : next_arg(&pos);
            if (!file) { fprintf(stderr, "shell-c: expected filename after 2>\n"); return -1; }
            cmd->errfile     = file;
            cmd->err_append  = 0;

        /* Background flag */
        } else if (strcmp(token, "&") == 0) {
            cmd->background = 1;

        } else {
            if (cmd->argc >= MAX_ARGS - 1) {
                fprintf(stderr, "shell-c: too many arguments\n");
                return -1;
            }
            /* Expand $VAR, $?, $(cmd) — may execute a subshell eagerly */
            cmd->argv[cmd->argc++] = expand_token(token, trace_mode);
        }
    }

    cmd->argv[cmd->argc] = NULL;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Pipeline parser                                                     */
/* ------------------------------------------------------------------ */

/*
 * Split input on '|' (depth-aware) then parse each segment into a
 * Command.  trace_mode is threaded through so cmd_subst can emit
 * [trace] lines for commands executed during expansion.
 *
 * An empty segment (stray "||", leading "|", or trailing "|") is a
 * syntax error rather than silently skipped: pl->cmds[] is indexed by
 * segment position, so dropping a segment without dropping its index
 * would desync it from ncmds and hand the executor a zeroed-out
 * Command (NULL argv) to exec. A whitespace-only *whole line* (a
 * single segment, no pipes) is still ignored silently, matching
 * ordinary blank-input handling.
 *
 * Returns 0 on success, -1 on error.
 */
int parse_pipeline(char *line, Pipeline *pl, int trace_mode) {
    memset(pl, 0, sizeof(Pipeline));

    char *segs[MAX_PIPES];
    int   nseg = split_pipes(line, segs, MAX_PIPES);
    if (nseg < 0) return -1;

    for (int i = 0; i < nseg; i++) {
        if (parse_command(segs[i], &pl->cmds[i], trace_mode) < 0)
            return -1;
        if (pl->cmds[i].argc == 0) {
            if (nseg == 1) continue;
            fprintf(stderr, "shell-c: syntax error near unexpected token `|'\n");
            last_exit = 1;
            return -1;
        }
        pl->ncmds++;
    }

    return 0;
}
