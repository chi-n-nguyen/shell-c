#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <sys/types.h>

#define MAX_INPUT   4096
#define MAX_ARGS    128
#define MAX_PIPES   16

/* Represents a single command in a pipeline */
typedef struct {
    char  *argv[MAX_ARGS];  /* null-terminated argument list */
    int    argc;
    char  *infile;          /* < redirection, NULL if none   */
    char  *outfile;         /* > redirection, NULL if none   */
    int    append;          /* 1 if >>, 0 if >               */
    char  *errfile;         /* 2> redirection, NULL if none  */
    int    err_append;      /* 1 if 2>>, 0 if 2>             */
    int    err_to_out;      /* 1 if 2>&1                     */
    int    background;      /* 1 if trailing &               */
} Command;

/* Represents a full pipeline: cmd1 | cmd2 | ... */
typedef struct {
    Command cmds[MAX_PIPES];
    int     ncmds;
} Pipeline;

/* Exit status of the last foreground pipeline; $? expands to this */
extern int last_exit;

/* Main REPL — pass stdin for interactive use, an open FILE* for scripts */
void shell_loop(int trace_mode, FILE *src);

/* Signal handler for reaping background children */
void sigchld_handler(int sig);

/* Parse raw input line into a Pipeline */
int  parse_pipeline(char *line, Pipeline *pl, int trace_mode);

/* Execute a parsed pipeline */
void execute_pipeline(Pipeline *pl, int trace_mode, const char *cmdline);

/*
 * Give the terminal to pgid, optionally send SIGCONT (send_cont != 0),
 * then wait for all pids with WUNTRACED.  If the job stops, it is added
 * to the job table and -1 is returned; otherwise last_exit is updated
 * and the exit status is returned.
 */
int  wait_foreground(pid_t pgid, pid_t *pids, int npids,
                     const char *cmdline, int send_cont);

/* Built-in command handler; returns 1 if handled, 0 otherwise */
int  run_builtin(Command *cmd);

/* Print shell prompt */
void print_prompt(void);

#endif
