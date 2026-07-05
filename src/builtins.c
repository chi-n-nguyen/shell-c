#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include "shell.h"
#include "history.h"
#include "jobs.h"

/* Parse %N or bare N job spec; returns job id, or -1 if spec is NULL */
static int parse_job_spec(const char *spec) {
    if (!spec) return -1;
    return (spec[0] == '%') ? atoi(spec + 1) : atoi(spec);
}

/*
 * Run a built-in command in the shell process itself (no fork).
 * Returns 1 if the command was a built-in, 0 otherwise.
 */
int run_builtin(Command *cmd) {
    if (cmd->argc == 0 || cmd->argv[0] == NULL)
        return 0;

    char *name = cmd->argv[0];

    /* cd [dir] — default to $HOME; cd - returns to previous directory */
    if (strcmp(name, "cd") == 0) {
        static char prev[4096];
        char *dir = cmd->argv[1];
        int print_dir = 0;

        if (!dir) {
            dir = getenv("HOME");
            if (!dir) { fprintf(stderr, "cd: HOME not set\n"); return 1; }
        } else if (strcmp(dir, "-") == 0) {
            if (!prev[0]) { fprintf(stderr, "cd: no previous directory\n"); return 1; }
            /* Copy out of prev before it is overwritten below */
            static char target[4096];
            strcpy(target, prev);
            dir = target;
            print_dir = 1;   /* POSIX: cd - prints the new directory */
        }

        char here[4096];
        if (!getcwd(here, sizeof(here))) here[0] = '\0';

        if (chdir(dir) < 0) {
            perror("cd");
            return 1;
        }
        if (here[0]) strcpy(prev, here);
        if (print_dir) { printf("%s\n", dir); fflush(stdout); }
        return 1;
    }

    /* exit [code] */
    if (strcmp(name, "exit") == 0) {
        int code = cmd->argv[1] ? atoi(cmd->argv[1]) : last_exit;
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
            /* export VAR — mark existing variable for export; no-op if unset */
            const char *val = getenv(cmd->argv[1]);
            if (val && setenv(cmd->argv[1], val, 1) != 0)
                perror("export");
            return 1;
        }
        *eq = '\0';
        if (setenv(cmd->argv[1], eq + 1, 1) != 0)
            perror("export");
        *eq = '=';
        return 1;
    }

    /* jobs — list active background/stopped jobs */
    if (strcmp(name, "jobs") == 0) {
        jobs_print_active();
        return 1;
    }

    /* fg [%N] — bring job to foreground */
    if (strcmp(name, "fg") == 0) {
        Job *j;
        int id = parse_job_spec(cmd->argv[1]);
        j = (id > 0) ? job_find_id(id) : job_last();
        if (!j) { fprintf(stderr, "fg: no current job\n"); return 1; }

        /* Snapshot job fields before removing — SIGCHLD handler may modify *j */
        pid_t  pgid = j->pgid;
        int    npids = j->npids;
        pid_t  pids[MAX_PIPES];
        char   cmdline[MAX_INPUT];
        int    jid = j->id;
        memcpy(pids, j->pids, (size_t)npids * sizeof(pid_t));
        strncpy(cmdline, j->cmdline, sizeof(cmdline) - 1);
        cmdline[sizeof(cmdline) - 1] = '\0';

        fprintf(stderr, "%s\n", cmdline);

        /* Block SIGCHLD before removal so handler can't race between
           job_remove and the wait inside wait_foreground */
        sigset_t mask, old;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &mask, &old);

        job_remove(jid);

        /* wait_foreground also blocks SIGCHLD; nesting SIG_SETMASK is safe:
           it saves the current (blocked) mask and restores it on return,
           then our final SIG_SETMASK below restores the original mask. */
        wait_foreground(pgid, pids, npids, cmdline, 1 /* send SIGCONT */);

        sigprocmask(SIG_SETMASK, &old, NULL);
        return 1;
    }

    /* bg [%N] — resume a stopped job in the background */
    if (strcmp(name, "bg") == 0) {
        Job *j;
        int id = parse_job_spec(cmd->argv[1]);
        j = (id > 0) ? job_find_id(id) : job_last_stopped();
        if (!j) { fprintf(stderr, "bg: no stopped job\n"); return 1; }
        if (j->status != JOB_STOPPED) {
            fprintf(stderr, "bg: job [%d] is not stopped\n", j->id);
            return 1;
        }
        j->status = JOB_RUNNING;
        kill(-(j->pgid), SIGCONT);
        fprintf(stderr, "[%d] %s &\n", j->id, j->cmdline);
        return 1;
    }

    return 0;
}
