#ifndef SHELL_H
#define SHELL_H

#include <sys/types.h>

#define MAX_INPUT   4096
#define MAX_ARGS    128
#define MAX_PIPES   16

/* Represents a single command in a pipeline */
typedef struct {
    char  *argv[MAX_ARGS];  /* null-terminated argument list */
    int    argc;
    char  *infile;          /* < redirection, NULL if none  */
    char  *outfile;         /* > redirection, NULL if none  */
    int    append;          /* 1 if >>, 0 if >              */
    int    background;      /* 1 if trailing &              */
} Command;

/* Represents a full pipeline: cmd1 | cmd2 | ... */
typedef struct {
    Command cmds[MAX_PIPES];
    int     ncmds;
} Pipeline;

/* Main REPL */
void shell_loop(int trace_mode);

/* Signal handler for reaping background children */
void sigchld_handler(int sig);

/* Parse raw input line into a Pipeline */
int  parse_pipeline(char *line, Pipeline *pl);

/* Execute a parsed pipeline */
void execute_pipeline(Pipeline *pl, int trace_mode);

/* Built-in command handler; returns 1 if handled, 0 otherwise */
int  run_builtin(Command *cmd);

/* Print shell prompt */
void print_prompt(void);

#endif
