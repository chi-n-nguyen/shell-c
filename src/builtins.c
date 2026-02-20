#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "shell.h"
#include "history.h"

/*
 * Run a built-in command in the shell process itself (no fork).
 * Returns 1 if the command was a built-in, 0 otherwise.
 */
int run_builtin(Command *cmd) {
    if (cmd->argc == 0 || cmd->argv[0] == NULL)
        return 0;

    char *name = cmd->argv[0];

    /* cd [dir] — default to $HOME if no argument */
    if (strcmp(name, "cd") == 0) {
        char *dir = cmd->argv[1];
        if (!dir) dir = getenv("HOME");
        if (!dir) { fprintf(stderr, "cd: HOME not set\n"); return 1; }
        if (chdir(dir) < 0)
            perror("cd");
        return 1;
    }

    /* exit [code] */
    if (strcmp(name, "exit") == 0) {
        int code = cmd->argv[1] ? atoi(cmd->argv[1]) : 0;
        history_free();
        exit(code);
    }

    /* history — print command history */
    if (strcmp(name, "history") == 0) {
        history_print();
        return 1;
    }

    /* export VAR=VALUE — set environment variable */
    if (strcmp(name, "export") == 0) {
        if (!cmd->argv[1]) {
            fprintf(stderr, "export: usage: export VAR=VALUE\n");
            return 1;
        }
        char *eq = strchr(cmd->argv[1], '=');
        if (!eq) {
            fprintf(stderr, "export: expected VAR=VALUE format\n");
            return 1;
        }
        /* Split into name and value for setenv */
        *eq = '\0';
        if (setenv(cmd->argv[1], eq + 1, 1) != 0)
            perror("export");
        *eq = '='; /* restore original string */
        return 1;
    }

    return 0;
}
