#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>
#include "shell.h"

#define MAX_JOBS 32

typedef enum { JOB_RUNNING = 0, JOB_STOPPED, JOB_DONE } JobStatus;

typedef struct {
    int      id;
    pid_t    pgid;
    pid_t    pids[MAX_PIPES];
    int      npids;
    char     cmdline[MAX_INPUT];
    volatile int status;  /* JobStatus; volatile for signal-handler writes */
    int      active;
} Job;

void  jobs_init(void);
int   job_add(pid_t pgid, pid_t *pids, int npids, const char *cmdline);
Job  *job_find_id(int id);
Job  *job_find_pid(pid_t pid);
Job  *job_last(void);
Job  *job_last_stopped(void);
void  job_remove(int id);
void  jobs_print_active(void);
void  jobs_notify_done(void);

#endif
