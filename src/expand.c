#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <pwd.h>
#include "shell.h"
#include "expand.h"

/* ------------------------------------------------------------------ */
/* Arena allocator                                                     */
/* ------------------------------------------------------------------ */

/* 64 KB is ample for all expanded tokens in one pipeline invocation. */
static char   arena[65536];
static size_t arena_off = 0;

void expand_arena_reset(void) { arena_off = 0; }

static char *expand_arena_alloc(size_t n) {
    if (arena_off + n > sizeof(arena)) return NULL;
    char *p = arena + arena_off;
    arena_off += n;
    return p;
}

/* ------------------------------------------------------------------ */
/* Command substitution                                                */
/* ------------------------------------------------------------------ */

/*
 * Fork a subshell whose stdout is captured via a pipe.
 * stdin is redirected to /dev/null so isatty() returns 0 inside the
 * subshell, suppressing tcsetpgrp terminal handoff.
 * SIGCHLD is blocked in the parent around fork+waitpid to prevent the
 * background-reaping handler from stealing the subshell's exit status.
 */
char *cmd_subst(const char *cmd, int trace_mode) {
    int pfd[2];
    if (pipe(pfd) < 0) { perror("pipe"); return NULL; }

    sigset_t mask, old;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &old);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(pfd[0]); close(pfd[1]);
        sigprocmask(SIG_SETMASK, &old, NULL);
        return NULL;
    }

    if (pid == 0) {
        /* Restore signal mask so inner pipelines work normally */
        sigprocmask(SIG_SETMASK, &old, NULL);

        close(pfd[0]);
        dup2(pfd[1], STDOUT_FILENO);
        close(pfd[1]);

        /* /dev/null stdin → isatty(STDIN_FILENO) == 0 → no tcsetpgrp */
        int null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) { dup2(null_fd, STDIN_FILENO); close(null_fd); }

        /* Parse and run the inner pipeline; supports pipes, redirections,
           and nested $() inside the substitution */
        char buf[MAX_INPUT];
        strncpy(buf, cmd, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        Pipeline pl;
        if (parse_pipeline(buf, &pl, trace_mode) == 0 && pl.ncmds > 0)
            execute_pipeline(&pl, trace_mode, cmd);

        exit(last_exit);
    }

    /* Parent: close write end then drain the pipe */
    close(pfd[1]);

    size_t cap = 4096, len = 0;
    char *out = malloc(cap);
    if (!out) {
        close(pfd[0]);
        waitpid(pid, NULL, 0);
        sigprocmask(SIG_SETMASK, &old, NULL);
        return NULL;
    }

    ssize_t n;
    while ((n = read(pfd[0], out + len, cap - len)) > 0) {
        len += (size_t)n;
        if (len == cap) {
            cap *= 2;
            char *tmp = realloc(out, cap);
            if (!tmp) {
                free(out);
                close(pfd[0]);
                waitpid(pid, NULL, 0);
                sigprocmask(SIG_SETMASK, &old, NULL);
                return NULL;
            }
            out = tmp;
        }
    }
    close(pfd[0]);
    waitpid(pid, NULL, 0);
    sigprocmask(SIG_SETMASK, &old, NULL);

    /* Strip trailing newlines — standard shell behaviour */
    while (len > 0 && out[len - 1] == '\n') len--;
    out[len] = '\0';
    return out;
}

/* ------------------------------------------------------------------ */
/* Token expansion                                                     */
/* ------------------------------------------------------------------ */

/*
 * Remove quotes and expand all $ sequences in tok, returning a pointer
 * into the arena. Handles embedded substitutions, e.g.
 * "/usr/$(uname -m)/lib".
 *
 * Supported:
 *   $(cmd)    — command substitution (recursive: nested $() ok)
 *   $?        — last foreground exit status
 *   $VARNAME  — environment variable (unset → empty string)
 *   '...'     — literal text; no $ or ~ expansion inside
 *   "..."     — groups text (e.g. spaces) but $ still expands inside
 *
 * The tokenizer (next_arg/split_pipes) treats a quoted region as
 * opaque so whitespace and '|' inside it aren't delimiters, but it
 * does not strip the quote characters or care which kind was used —
 * that happens here, in one pass, since quote type must be known to
 * decide whether $ still expands.
 *
 * If tok contains no '$', quote character, or leading '~' the
 * original pointer is returned unchanged (no arena allocation).
 */
char *expand_token(const char *tok, int trace_mode) {
    if (!strchr(tok, '$') && !strchr(tok, '\'') && !strchr(tok, '"') && tok[0] != '~')
        return (char *)tok;

    /* Build into a local buffer first, then copy to the arena */
    char tmp[MAX_INPUT * 2];
    char *out = tmp;
    char *lim = tmp + sizeof(tmp) - 1;
    const char *p = tok;
    char in_quote = 0;   /* 0, '\'', or '"' — quote currently open */

    while (*p && out < lim) {
        /* Quote characters delimit a region but are never copied */
        if (!in_quote && (*p == '\'' || *p == '"')) { in_quote = *p; p++; continue; }
        if (in_quote  && *p == in_quote)             { in_quote = 0;  p++; continue; }

        /* Single-quoted text is fully literal: no $ or ~ expansion */
        if (in_quote == '\'') { *out++ = *p++; continue; }

        /* Tilde expansion — only at the very start of the token, and
           only when nothing (not even a quote char) precedes it */
        if (*p == '~' && p == tok) {
            const char *home = NULL;

            if (*(p + 1) == '/' || *(p + 1) == '\0') {
                /* ~/...  or bare ~  →  $HOME, fall back to passwd db */
                home = getenv("HOME");
                if (!home) {
                    struct passwd *pw = getpwuid(getuid());
                    if (pw) home = pw->pw_dir;
                }
                p++; /* skip '~'; '/' (if any) copied next iteration */
            } else {
                /* ~user/...  →  look up user in passwd db */
                const char *us = p + 1, *ue = us;
                while (*ue && *ue != '/') ue++;
                char username[256];
                size_t ulen = (size_t)(ue - us);
                if (ulen < sizeof(username)) {
                    memcpy(username, us, ulen);
                    username[ulen] = '\0';
                    struct passwd *pw = getpwnam(username);
                    if (pw) { home = pw->pw_dir; p = ue; }
                }
                /* unknown user — leave ~ in place, fall through */
            }

            if (home) {
                size_t hlen = strlen(home);
                if (out + hlen < lim) { memcpy(out, home, hlen); out += hlen; }
                continue;
            }
        }

        if (*p != '$') { *out++ = *p++; continue; }

        if (*(p + 1) == '(') {
            /* ---- command substitution $(...)  ---- */
            /* Walk forward counting parenthesis depth to find the
               match; a paren inside a quoted string doesn't count
               (e.g. $(echo "a)b") ), mirroring next_arg/split_pipes. */
            int depth = 1;
            const char *inner = p + 2;
            const char *q = inner;
            char iq = 0;
            while (*q && depth > 0) {
                if (iq) {
                    if (*q == iq) iq = 0;
                } else if (*q == '\'' || *q == '"') {
                    iq = *q;
                } else if (*q == '(') {
                    depth++;
                } else if (*q == ')') {
                    depth--;
                }
                if (depth > 0) q++;
                else            break;
            }
            /* inner[0..q-inner) is the command; *q == ')' (or '\0') */
            char cmd_buf[MAX_INPUT];
            size_t clen = (size_t)(q - inner);
            if (clen >= sizeof(cmd_buf)) clen = sizeof(cmd_buf) - 1;
            memcpy(cmd_buf, inner, clen);
            cmd_buf[clen] = '\0';

            char *result = cmd_subst(cmd_buf, trace_mode);
            if (result) {
                size_t rlen = strlen(result);
                if (out + rlen < lim) { memcpy(out, result, rlen); out += rlen; }
                free(result);
            }
            p = (*q == ')') ? q + 1 : q;

        } else if (*(p + 1) == '?') {
            /* ---- $?  ----
             * snprintf's return value is the length it would have
             * written, not the length it did — using it unchecked
             * (as size available) can walk `out` past `lim`. Render
             * to a small local buffer first and bounds-check it like
             * every other expansion below. */
            char numbuf[16];
            int  n = snprintf(numbuf, sizeof(numbuf), "%d", last_exit);
            if (n > 0 && out + n < lim) { memcpy(out, numbuf, (size_t)n); out += n; }
            p += 2;

        } else if (isalpha((unsigned char)*(p + 1)) || *(p + 1) == '_') {
            /* ---- $VARNAME  ---- */
            const char *vs = p + 1, *ve = vs;
            while (*ve && (isalnum((unsigned char)*ve) || *ve == '_')) ve++;
            char varname[256];
            size_t vlen = (size_t)(ve - vs);
            if (vlen >= sizeof(varname)) vlen = sizeof(varname) - 1;
            memcpy(varname, vs, vlen);
            varname[vlen] = '\0';
            const char *val = getenv(varname);
            if (val) {
                size_t vlen2 = strlen(val);
                if (out + vlen2 < lim) { memcpy(out, val, vlen2); out += vlen2; }
            }
            /* unset variable expands to empty string */
            p = ve;

        } else {
            /* Lone '$' with no special suffix — pass through */
            *out++ = *p++;
        }
    }
    *out = '\0';

    size_t result_len = (size_t)(out - tmp) + 1;
    char *slot = expand_arena_alloc(result_len);
    if (!slot) return (char *)tok;   /* arena full — fall back to unexpanded */
    memcpy(slot, tmp, result_len);
    return slot;
}
