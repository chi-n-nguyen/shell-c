#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "history.h"
#include "shell.h"   /* MAX_INPUT */

static char *entries[HISTORY_MAX];
static int   count = 0;

/* Returns path to ~/.shell-c_history, or NULL if $HOME is unset */
static const char *history_path(void) {
    static char path[4096];
    const char *home = getenv("HOME");
    if (!home) return NULL;
    snprintf(path, sizeof(path), "%s/.shell-c_history", home);
    return path;
}

/* Add a line directly to the in-memory ring buffer (no file write) */
static void ring_add(const char *line) {
    if (count < HISTORY_MAX) {
        entries[count++] = strdup(line);
    } else {
        free(entries[0]);
        memmove(entries, entries + 1, (HISTORY_MAX - 1) * sizeof(char *));
        entries[HISTORY_MAX - 1] = strdup(line);
    }
}

void history_init(void) {
    memset(entries, 0, sizeof(entries));
    count = 0;

    const char *path = history_path();
    if (!path) return;

    FILE *f = fopen(path, "r");
    if (!f) return;   /* no history file yet — not an error */

    char line[MAX_INPUT];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0]) ring_add(line);
    }
    fclose(f);
}

void history_add(const char *line) {
    if (!line || line[0] == '\0') return;
    ring_add(line);
}

void history_print(void) {
    for (int i = 0; i < count; i++)
        printf("  %3d  %s\n", i + 1, entries[i]);
}

/* Returns entry at 1-based index, or NULL if out of range */
char *history_get(int index) {
    if (index < 1 || index > count) return NULL;
    return entries[index - 1];
}

int history_count(void) { return count; }

/*
 * Persist the current ring buffer to ~/.shell-c_history (rewriting it
 * so the file never grows beyond HISTORY_MAX lines), then free memory.
 */
void history_free(void) {
    const char *path = history_path();
    if (path) {
        FILE *f = fopen(path, "w");
        if (f) {
            for (int i = 0; i < count; i++)
                fprintf(f, "%s\n", entries[i]);
            fclose(f);
        }
    }

    for (int i = 0; i < count; i++) {
        free(entries[i]);
        entries[i] = NULL;
    }
    count = 0;
}
