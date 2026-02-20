#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shell.h"

/*
 * Tokenise a single command segment (no pipes) into a Command struct.
 * Handles: <infile, >outfile, >>outfile, trailing &
 * Returns 0 on success, -1 on error.
 */
static int parse_command(char *segment, Command *cmd) {
    memset(cmd, 0, sizeof(Command));

    char *token;
    char *rest = segment;

    while ((token = strtok_r(rest, " \t", &rest)) != NULL) {
        /* Input redirection */
        if (strcmp(token, "<") == 0) {
            token = strtok_r(rest, " \t", &rest);
            if (!token) { fprintf(stderr, "shell-c: expected filename after <\n"); return -1; }
            cmd->infile = token;

        /* Append output redirection */
        } else if (strcmp(token, ">>") == 0) {
            token = strtok_r(rest, " \t", &rest);
            if (!token) { fprintf(stderr, "shell-c: expected filename after >>\n"); return -1; }
            cmd->outfile = token;
            cmd->append  = 1;

        /* Output redirection */
        } else if (strcmp(token, ">") == 0) {
            token = strtok_r(rest, " \t", &rest);
            if (!token) { fprintf(stderr, "shell-c: expected filename after >\n"); return -1; }
            cmd->outfile = token;
            cmd->append  = 0;

        /* Background flag */
        } else if (strcmp(token, "&") == 0) {
            cmd->background = 1;

        } else {
            if (cmd->argc >= MAX_ARGS - 1) {
                fprintf(stderr, "shell-c: too many arguments\n");
                return -1;
            }
            cmd->argv[cmd->argc++] = token;
        }
    }

    cmd->argv[cmd->argc] = NULL;
    return 0;
}

/*
 * Split input on '|' then parse each segment into a Command.
 * Populates Pipeline *pl. Returns 0 on success, -1 on parse error.
 */
int parse_pipeline(char *line, Pipeline *pl) {
    memset(pl, 0, sizeof(Pipeline));

    char *segments[MAX_PIPES];
    int   nseg = 0;

    char *rest = line;
    char *seg;
    while ((seg = strtok_r(rest, "|", &rest)) != NULL) {
        if (nseg >= MAX_PIPES) {
            fprintf(stderr, "shell-c: too many pipes\n");
            return -1;
        }
        segments[nseg++] = seg;
    }

    for (int i = 0; i < nseg; i++) {
        if (parse_command(segments[i], &pl->cmds[i]) < 0)
            return -1;
        if (pl->cmds[i].argc == 0)
            continue;
        pl->ncmds++;
    }

    return 0;
}
