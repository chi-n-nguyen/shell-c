#include <stdio.h>
#include <string.h>
#include "jobs.h"

static Job table[MAX_JOBS];
static int next_id = 1;

void jobs_init(void) {
    memset(table, 0, sizeof(table));
    next_id = 1;
}

int job_add(pid_t pgid, pid_t *pids, int npids, const char *cmdline) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (table[i].active) continue;
        table[i].id     = next_id++;
        table[i].pgid   = pgid;
        table[i].npids  = npids;
        memcpy(table[i].pids, pids, (size_t)npids * sizeof(pid_t));
        strncpy(table[i].cmdline, cmdline, sizeof(table[i].cmdline) - 1);
        table[i].cmdline[sizeof(table[i].cmdline) - 1] = '\0';
        table[i].status = JOB_RUNNING;
        table[i].active = 1;
        return table[i].id;
    }
    return -1;
}

Job *job_find_id(int id) {
    for (int i = 0; i < MAX_JOBS; i++)
        if (table[i].active && table[i].id == id) return &table[i];
    return NULL;
}

Job *job_find_pid(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!table[i].active) continue;
        for (int j = 0; j < table[i].npids; j++)
            if (table[i].pids[j] == pid) return &table[i];
    }
    return NULL;
}

Job *job_last(void) {
    Job *last = NULL;
    for (int i = 0; i < MAX_JOBS; i++)
        if (table[i].active) last = &table[i];
    return last;
}

Job *job_last_stopped(void) {
    Job *last = NULL;
    for (int i = 0; i < MAX_JOBS; i++)
        if (table[i].active && table[i].status == JOB_STOPPED) last = &table[i];
    return last;
}

void job_remove(int id) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (table[i].active && table[i].id == id) {
            memset(&table[i], 0, sizeof(Job));
            return;
        }
    }
}

void jobs_print_active(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!table[i].active) continue;
        const char *state =
            (table[i].status == JOB_STOPPED) ? "stopped" :
            (table[i].status == JOB_DONE)    ? "done"    : "running";
        printf("[%d] %-8s %s\n", table[i].id, state, table[i].cmdline);
    }
}

void jobs_notify_done(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!table[i].active || table[i].status != JOB_DONE) continue;
        fprintf(stderr, "[%d] done\t%s\n", table[i].id, table[i].cmdline);
        memset(&table[i], 0, sizeof(Job));
    }
}
