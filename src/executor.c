#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include "shell.h"
#include "jobs.h"

int last_exit = 0;

/*
 * Apply <, >, >>, 2>, 2>>, 2>&1 redirections in the child after pipe
 * fds are wired.  File redirections override pipe wiring.
 * stdout is redirected before stderr so that 2>&1 captures the
 * already-redirected stdout (the common "> file 2>&1" idiom).
 */
static void apply_redirections(Command *cmd) {
    if (cmd->infile) {
        int fd = open(cmd->infile, O_RDONLY);
        if (fd < 0) { perror(cmd->infile); exit(1); }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (cmd->outfile) {
        int flags = O_WRONLY | O_CREAT | (cmd->append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->outfile, flags, 0644);
        if (fd < 0) { perror(cmd->outfile); exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    /* Stderr redirections — applied after stdout so 2>&1 sees the
       final stdout destination, matching POSIX left-to-right semantics
       for the common "> file 2>&1" pattern. */
    if (cmd->err_to_out) {
        dup2(STDOUT_FILENO, STDERR_FILENO);
    } else if (cmd->errfile) {
        int flags = O_WRONLY | O_CREAT | (cmd->err_append ? O_APPEND : O_TRUNC);
        int fd = open(cmd->errfile, flags, 0644);
        if (fd < 0) { perror(cmd->errfile); exit(1); }
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
}

/*
 * Give the terminal to pgid, optionally send SIGCONT, then wait for all
 * pids (WUNTRACED so Ctrl+Z is caught).  SIGCHLD is blocked for the
 * duration so the handler cannot steal our children via WNOHANG.
 *
 * Returns the last command's exit status, or -1 if the job stopped
 * (in which case it has been added to the job table and a notification
 * has been printed).
 */
int wait_foreground(pid_t pgid, pid_t *pids, int npids,
                    const char *cmdline, int send_cont) {
    sigset_t mask, old;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &old);

    if (isatty(STDIN_FILENO))
        tcsetpgrp(STDIN_FILENO, pgid);

    if (send_cont)
        kill(-pgid, SIGCONT);

    int last_status = 0;
    for (int i = 0; i < npids; i++) {
        int s;
        waitpid(pids[i], &s, WUNTRACED);
        if (i == npids - 1)
            last_status = s;
    }

    if (isatty(STDIN_FILENO))
        tcsetpgrp(STDIN_FILENO, getpgrp());

    int ret;
    if (WIFSTOPPED(last_status)) {
        int jid = job_add(pgid, pids, npids, cmdline);
        fprintf(stderr, "\n[%d]+ stopped\t%s\n", jid, cmdline);
        ret = -1;
    } else {
        last_exit = WIFEXITED(last_status) ? WEXITSTATUS(last_status)
                                            : 128 + WTERMSIG(last_status);
        ret = last_exit;
    }

    sigprocmask(SIG_SETMASK, &old, NULL);
    return ret;
}

/*
 * Execute a pipeline of one or more commands.
 * Creates n-1 pipes for n commands, forks one child per stage.
 * All children share a process group so signals reach the whole pipeline.
 * SIGCHLD is blocked around the fork loop and job_add to eliminate races.
 * --trace logs each fork/exec/pipe decision to stderr.
 */
void execute_pipeline(Pipeline *pl, int trace_mode, const char *cmdline) {
    int n = pl->ncmds;

    /* pipe_fds[i] = {read_end, write_end} connecting cmd[i] -> cmd[i+1] */
    int pipe_fds[MAX_PIPES][2];

    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipe_fds[i]) < 0) {
            perror("pipe");
            return;
        }
        if (trace_mode)
            fprintf(stderr, "[trace] pipe created: fd[%d->%d]\n",
                    pipe_fds[i][1], pipe_fds[i][0]);
    }

    /* Block SIGCHLD around forks so the handler can't race with job_add */
    sigset_t mask, old;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &old);

    pid_t pids[MAX_PIPES];
    pid_t pgid = 0;  /* shared process group; set to pids[0] on first fork */

    for (int i = 0; i < n; i++) {
        Command *cmd = &pl->cmds[i];

        int in_fd  = (i == 0)     ? STDIN_FILENO  : pipe_fds[i-1][0];
        int out_fd = (i == n - 1) ? STDOUT_FILENO : pipe_fds[i][1];

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            sigprocmask(SIG_SETMASK, &old, NULL);
            return;
        }

        if (pid == 0) {
            /* Restore original signal mask so the child is not born blocked */
            sigprocmask(SIG_SETMASK, &old, NULL);

            /* Join (or create) the pipeline's process group */
            setpgid(0, pgid == 0 ? 0 : pgid);

            /* Restore default signal handlers so Ctrl+C / Ctrl+Z work */
            signal(SIGINT,  SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            /* Step 1: wire pipe ends to stdin/stdout */
            if (in_fd  != STDIN_FILENO)  dup2(in_fd,  STDIN_FILENO);
            if (out_fd != STDOUT_FILENO) dup2(out_fd, STDOUT_FILENO);

            /* Step 2: close all original pipe fds */
            for (int j = 0; j < n - 1; j++) {
                close(pipe_fds[j][0]);
                close(pipe_fds[j][1]);
            }

            /* Step 3: file redirections (< > >>) override pipe wiring */
            apply_redirections(cmd);

            if (trace_mode)
                fprintf(stderr, "[trace] exec: %s (pid=%d)\n", cmd->argv[0], getpid());

            execvp(cmd->argv[0], cmd->argv);
            perror(cmd->argv[0]);
            exit(127);
        }

        /* Parent: assign child to the pipeline's process group */
        if (pgid == 0) pgid = pid;
        setpgid(pid, pgid);

        /* Close pipe ends the parent no longer needs */
        if (i > 0)     close(pipe_fds[i-1][0]);
        if (i < n - 1) close(pipe_fds[i][1]);

        pids[i] = pid;

        if (trace_mode)
            fprintf(stderr, "[trace] forked pid=%d for cmd=%s\n", pid, cmd->argv[0]);
    }

    /* Background: register job, then return without waiting */
    if (pl->cmds[n-1].background) {
        int jid = job_add(pgid, pids, n, cmdline);
        fprintf(stderr, "[%d] %d\n", jid, pids[n-1]);
        sigprocmask(SIG_SETMASK, &old, NULL);
        return;
    }

    /* Foreground: unblock SIGCHLD (wait_foreground re-blocks internally) */
    sigprocmask(SIG_SETMASK, &old, NULL);
    wait_foreground(pgid, pids, n, cmdline, 0);
}
