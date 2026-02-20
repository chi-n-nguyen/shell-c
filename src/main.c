#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "shell.h"
#include "history.h"

int main(int argc, char *argv[]) {
    int trace_mode = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--trace") == 0)
            trace_mode = 1;
    }

    /* Ignore SIGINT and SIGTSTP in the shell process itself;
       child processes inherit default handlers via execvp */
    signal(SIGINT,  SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGCHLD, sigchld_handler);

    history_init();
    shell_loop(trace_mode);
    history_free();

    return 0;
}
