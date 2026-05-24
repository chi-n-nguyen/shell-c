#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "shell.h"
#include "history.h"
#include "jobs.h"

int main(int argc, char *argv[]) {
    int   trace_mode = 0;
    FILE *src        = stdin;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--trace") == 0) {
            trace_mode = 1;
        } else if (src == stdin) {
            /* First non-flag argument is treated as a script file */
            src = fopen(argv[i], "r");
            if (!src) { perror(argv[i]); return 1; }
        }
    }

    /* Shell ignores Ctrl+C and Ctrl+Z; children restore defaults after fork */
    struct sigaction sa_ign;
    memset(&sa_ign, 0, sizeof(sa_ign));
    sa_ign.sa_handler = SIG_IGN;
    sigaction(SIGINT,  &sa_ign, NULL);
    sigaction(SIGTSTP, &sa_ign, NULL);

    /* Reap background children and detect stopped jobs.
       SA_NOCLDSTOP removed so SIGCHLD fires on Ctrl+Z as well as exit. */
    struct sigaction sa_chld;
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = sigchld_handler;
    sa_chld.sa_flags   = SA_RESTART;
    sigaction(SIGCHLD, &sa_chld, NULL);

    history_init();
    jobs_init();
    shell_loop(trace_mode, src);
    history_free();

    if (src != stdin) fclose(src);
    return 0;
}
