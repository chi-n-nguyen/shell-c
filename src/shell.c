#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "shell.h"
#include "history.h"

/* Reap finished background children without blocking.
 * Uses write() instead of fprintf() — write() is async-signal-safe;
 * fprintf() is not and must never be called from a signal handler. */
void sigchld_handler(int sig) {
    (void)sig;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        /* Build "[<pid>] done\n" with only async-signal-safe calls */
        char buf[32];
        int  i = sizeof(buf);
        buf[--i] = '\n';
        buf[--i] = 'e'; buf[--i] = 'n'; buf[--i] = 'o'; buf[--i] = 'd';
        buf[--i] = ' '; buf[--i] = ']';
        pid_t tmp = pid;
        do { buf[--i] = '0' + (int)(tmp % 10); tmp /= 10; } while (tmp > 0);
        buf[--i] = '[';
        write(STDERR_FILENO, buf + i, sizeof(buf) - i);
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

void shell_loop(int trace_mode) {
    char input[MAX_INPUT];
    Pipeline pl;

    while (1) {
        print_prompt();

        if (!fgets(input, sizeof(input), stdin)) {
            /* EOF (Ctrl+D) */
            printf("\n");
            break;
        }

        /* Strip trailing newline */
        input[strcspn(input, "\n")] = '\0';

        if (strlen(input) == 0)
            continue;

        history_add(input);

        if (parse_pipeline(input, &pl) < 0)
            continue;

        if (pl.ncmds == 0)
            continue;

        /* Check for single built-in before forking */
        if (pl.ncmds == 1 && run_builtin(&pl.cmds[0]))
            continue;

        execute_pipeline(&pl, trace_mode);
    }
}
