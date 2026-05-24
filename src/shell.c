#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "shell.h"
#include "history.h"
#include "jobs.h"
#include "expand.h"

/*
 * Reap finished/stopped background children without blocking.
 * Updates job table status; main loop prints notifications via jobs_notify_done().
 * Uses only async-signal-safe operations: waitpid() and volatile struct field writes.
 */
void sigchld_handler(int sig) {
    (void)sig;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        Job *j = job_find_pid(pid);
        if (!j) continue;
        if (WIFSTOPPED(status))
            j->status = JOB_STOPPED;
        else if (WIFEXITED(status) || WIFSIGNALED(status))
            j->status = JOB_DONE;
    }
}

void print_prompt(void) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)))
        printf("%s $ ", cwd);
    else
        printf("$ ");
    fflush(stdout);
}

/*
 * Main read-eval-print loop.
 * src == stdin  → interactive mode: print prompts, add history, notify bg jobs.
 * src == file   → script mode: suppress prompts and history; commands are read
 *                 and executed silently, matching standard shell script behaviour.
 */
void shell_loop(int trace_mode, FILE *src) {
    char input[MAX_INPUT];
    char cmdline[MAX_INPUT];
    Pipeline pl;
    int  interactive = isatty(fileno(src));

    while (1) {
        if (interactive) {
            jobs_notify_done();
            print_prompt();
        }

        if (!fgets(input, sizeof(input), src)) {
            if (interactive) printf("\n");
            break;
        }

        input[strcspn(input, "\n")] = '\0';

        if (strlen(input) == 0)
            continue;

        /* Save original line before parse_pipeline mutates it with strtok_r */
        strncpy(cmdline, input, sizeof(cmdline) - 1);
        cmdline[sizeof(cmdline) - 1] = '\0';

        /* Only record history in interactive sessions */
        if (interactive)
            history_add(cmdline);

        /* Reset expansion arena once per command line */
        expand_arena_reset();

        if (parse_pipeline(input, &pl, trace_mode) < 0)
            continue;

        if (pl.ncmds == 0)
            continue;

        /* Check for single built-in before forking */
        if (pl.ncmds == 1 && run_builtin(&pl.cmds[0]))
            continue;

        execute_pipeline(&pl, trace_mode, cmdline);
    }
}
