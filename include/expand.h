#ifndef EXPAND_H
#define EXPAND_H

#include <stddef.h>

/*
 * Per-command-line arena for expanded token strings.
 * Call expand_arena_reset() once per top-level shell_loop iteration
 * before invoking parse_pipeline; all expansions within that iteration
 * (including those inside nested cmd_subst calls) share the arena and
 * are valid until the next reset.
 */
void  expand_arena_reset(void);

/*
 * Expand $VAR, $?, and $(cmd) within a single token.
 * Returns tok unchanged if no '$' is found; otherwise returns a pointer
 * into the arena containing the fully expanded string.
 */
char *expand_token(const char *tok, int trace_mode);

/*
 * Fork a subshell, execute cmd with its stdout captured via a pipe,
 * strip trailing newlines, and return a heap-allocated result string.
 * Caller must free() the returned pointer.  Returns NULL on error.
 */
char *cmd_subst(const char *cmd, int trace_mode);

#endif
