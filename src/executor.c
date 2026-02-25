#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include "shell.h"

/*
 * Apply < and > file redirections in the child after pipe fds are wired.
 * File redirections override pipe wiring when both appear on the same stage.
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
}

/*
 * Execute a pipeline of one or more commands.
 * Creates n-1 pipes for n commands, forks one child per stage.
 * All children share a process group so signals reach the whole pipeline.
 * --trace logs each fork/exec/pipe decision to stderr.
 */
void execute_pipeline(Pipeline *pl, int trace_mode) {
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

    pid_t pids[MAX_PIPES];
    pid_t pgid = 0;  /* shared process group; set to pids[0] on first fork */

    for (int i = 0; i < n; i++) {
        Command *cmd = &pl->cmds[i];

        int in_fd  = (i == 0)     ? STDIN_FILENO  : pipe_fds[i-1][0];
        int out_fd = (i == n - 1) ? STDOUT_FILENO : pipe_fds[i][1];

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return; }

        if (pid == 0) {
            /* Child: join (or create) the pipeline's process group.
               Both parent and child call setpgid to eliminate the race.
               setpgid(0,0) makes this process its own group leader;
               setpgid(0,pgid) joins the group created by the first child. */
            setpgid(0, pgid == 0 ? 0 : pgid);

            /* Restore default signal handlers so Ctrl+C / Ctrl+Z work */
            signal(SIGINT,  SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            /* Step 1: wire pipe ends to stdin/stdout */
            if (in_fd  != STDIN_FILENO)  dup2(in_fd,  STDIN_FILENO);
            if (out_fd != STDOUT_FILENO) dup2(out_fd, STDOUT_FILENO);

            /* Step 2: close all original pipe fds — stdin/stdout retain
               the references so the pipe stays open */
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

    /* Background: don't wait (SIGCHLD handler reaps later) */
    if (pl->cmds[n-1].background) {
        fprintf(stderr, "[%d] running in background\n", pids[n-1]);
        return;
    }

    /* Foreground: hand the terminal to the pipeline's process group so
       Ctrl+C delivers SIGINT to the pipeline, not the shell */
    if (isatty(STDIN_FILENO))
        tcsetpgrp(STDIN_FILENO, pgid);

    for (int i = 0; i < n; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (trace_mode)
            fprintf(stderr, "[trace] pid=%d exited with status=%d\n",
                    pids[i], WEXITSTATUS(status));
    }

    /* Reclaim the terminal for the shell */
    if (isatty(STDIN_FILENO))
        tcsetpgrp(STDIN_FILENO, getpgrp());
}
