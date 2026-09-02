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
 * appears inside a $(...) command substitution or a '...'/"..." quoted
 * string (quoting applies at any depth, so a quote opened inside a
 * $(...) still masks '|' and parens until it closes).  Writes pointers
 * to NUL-terminated segments into segs[] and returns the count, or -1
 * if there are more than `max` segments.
 */
static int split_pipes(char *line, char **segs, int max) {
    int   n     = 0;
    int   depth = 0;   /* $( nesting depth */
    char  quote = 0;   /* open quote char ('\'' or '"'), 0 if none */
    char *start = line;

    for (char *p = line; *p; p++) {
        if (quote) {
            if (*p == quote) quote = 0;
            continue;
        }
        if (*p == '\'' || *p == '"') { quote = *p; continue; }
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
 * Whitespace and '|' inside $(...) or inside a '...'/"..." quoted
 * string are not treated as delimiters, so $(date +%Y) and "two words"
 * each come back as a single token — quote characters themselves are
 * left in place (expand_token() strips them and applies single- vs
 * double-quote expansion rules). If a quote is never closed, *err is
 * set to 1 and the rest of the segment is returned as one token, for
 * the caller to reject as a syntax error.
 * Returns NULL when no more tokens remain.
 */
static char *next_arg(char **pos, int *err) {
    char *p = *pos;

    /* skip leading whitespace */
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) { *pos = p; return NULL; }

    char *start = p;
    int   depth = 0;
    char  quote = 0;

    while (*p) {
        if (quote) {
            if (*p == quote) quote = 0;
            p++;
            continue;
        }
        if (*p == '\'' || *p == '"') { quote = *p; p++; continue; }
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

    if (quote && err) *err = 1;

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
 * (and redirection filenames) are passed through expand_token() for
 * quote removal and $VAR / $? / $() expansion.
 * Returns 0 on success, -1 on error (including an unterminated quote,
 * checked right after every next_arg() call so a garbled token from a
 * dangling quote is never handed to expand_token — which could
 * otherwise eagerly run a $(...) buried in it before the syntax error
 * is caught).
 */
static int parse_command(char *segment, Command *cmd, int trace_mode) {
    memset(cmd, 0, sizeof(Command));

    char *pos = segment;
    char *token;
    int   qerr = 0;

    while ((token = next_arg(&pos, &qerr)) != NULL) {
        if (qerr) { fprintf(stderr, "shell-c: unterminated quote\n"); return -1; }

        /* Input redirection */
        if (strcmp(token, "<") == 0) {
            char *file = next_arg(&pos, &qerr);
            if (qerr) { fprintf(stderr, "shell-c: unterminated quote\n"); return -1; }
            if (!file) { fprintf(stderr, "shell-c: expected filename after <\n"); return -1; }
            cmd->infile = expand_token(file, trace_mode);

        /* Append redirection */
        } else if (strcmp(token, ">>") == 0) {
            char *file = next_arg(&pos, &qerr);
            if (qerr) { fprintf(stderr, "shell-c: unterminated quote\n"); return -1; }
            if (!file) { fprintf(stderr, "shell-c: expected filename after >>\n"); return -1; }
            cmd->outfile = expand_token(file, trace_mode);
            cmd->append  = 1;

        /* Output redirection */
        } else if (strcmp(token, ">") == 0) {
            char *file = next_arg(&pos, &qerr);
            if (qerr) { fprintf(stderr, "shell-c: unterminated quote\n"); return -1; }
            if (!file) { fprintf(stderr, "shell-c: expected filename after >\n"); return -1; }
            cmd->outfile = expand_token(file, trace_mode);
            cmd->append  = 0;

        /* Stderr → stdout: 2>&1 */
        } else if (strcmp(token, "2>&1") == 0) {
            cmd->err_to_out = 1;

        /* Stderr append: 2>>file  or  2>> file */
        } else if (strncmp(token, "2>>", 3) == 0) {
            char *file = token[3] ? token + 3 : next_arg(&pos, &qerr);
            if (qerr) { fprintf(stderr, "shell-c: unterminated quote\n"); return -1; }
            if (!file) { fprintf(stderr, "shell-c: expected filename after 2>>\n"); return -1; }
            cmd->errfile     = expand_token(file, trace_mode);
            cmd->err_append  = 1;

        /* Stderr redirect: 2>file  or  2> file */
        } else if (strncmp(token, "2>", 2) == 0) {
            char *file = token[2] ? token + 2 : next_arg(&pos, &qerr);
            if (qerr) { fprintf(stderr, "shell-c: unterminated quote\n"); return -1; }
            if (!file) { fprintf(stderr, "shell-c: expected filename after 2>\n"); return -1; }
            cmd->errfile     = expand_token(file, trace_mode);
            cmd->err_append  = 0;

        /* Background flag */
        } else if (strcmp(token, "&") == 0) {
            cmd->background = 1;

        } else {
            if (cmd->argc >= MAX_ARGS - 1) {
                fprintf(stderr, "shell-c: too many arguments\n");
                return -1;
            }
            /* Expand quotes, $VAR, $?, $(cmd) — may execute a subshell eagerly */
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
