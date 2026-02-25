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

    /* Shell ignores Ctrl+C and Ctrl+Z; children restore defaults after fork */
    struct sigaction sa_ign;
    memset(&sa_ign, 0, sizeof(sa_ign));
    sa_ign.sa_handler = SIG_IGN;
    sigaction(SIGINT,  &sa_ign, NULL);
    sigaction(SIGTSTP, &sa_ign, NULL);

    /* Reap background children asynchronously.
       SA_RESTART: auto-restart fgets after the handler returns.
       SA_NOCLDSTOP: suppress SIGCHLD on Ctrl+Z — only fire on child exit. */
    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = sigchld_handler;
    sa_chld.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    history_init();
    shell_loop(trace_mode);
    history_free();

    return 0;
}
