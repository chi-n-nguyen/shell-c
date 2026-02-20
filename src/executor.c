#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include "shell.h"

/*
 * Set up file descriptor redirections for a child process.
 * Called after fork(), before execvp().
 */
static void apply_redirections(Command *cmd, int in_fd, int out_fd) {
    /* Pipe input from previous stage */
    if (in_fd != STDIN_FILENO) {
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    }

    /* Pipe output to next stage */
    if (out_fd != STDOUT_FILENO) {
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }

    /* File input redirection: < infile */
    if (cmd->infile) {
        int fd = open(cmd->infile, O_RDONLY);
        if (fd < 0) { perror(cmd->infile); exit(1); }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    /* File output redirection: > outfile or >> outfile */
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
 * Creates n-1 pipes for n commands, forks a child per command.
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

    for (int i = 0; i < n; i++) {
        Command *cmd = &pl->cmds[i];

        int in_fd  = (i == 0)     ? STDIN_FILENO  : pipe_fds[i-1][0];
        int out_fd = (i == n - 1) ? STDOUT_FILENO : pipe_fds[i][1];

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return; }

        if (pid == 0) {
            /* Child: restore default signal handlers so Ctrl+C works */
            signal(SIGINT,  SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            apply_redirections(cmd, in_fd, out_fd);

            /* Close all pipe fds the child doesn't need */
            for (int j = 0; j < n - 1; j++) {
                close(pipe_fds[j][0]);
                close(pipe_fds[j][1]);
            }

            if (trace_mode)
                fprintf(stderr, "[trace] exec: %s (pid=%d)\n", cmd->argv[0], getpid());

            execvp(cmd->argv[0], cmd->argv);
            perror(cmd->argv[0]);
            exit(127);
        }

        /* Parent: close pipe ends it no longer needs */
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

    /* Foreground: wait for all children in the pipeline */
    for (int i = 0; i < n; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (trace_mode)
            fprintf(stderr, "[trace] pid=%d exited with status=%d\n",
                    pids[i], WEXITSTATUS(status));
    }
}
