#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "history.h"

static char *entries[HISTORY_MAX];
static int   count = 0;

void history_init(void) {
    memset(entries, 0, sizeof(entries));
    count = 0;
}

void history_add(const char *line) {
    if (!line || strlen(line) == 0)
        return;

    if (count < HISTORY_MAX) {
        entries[count++] = strdup(line);
    } else {
        /* Ring buffer: drop oldest entry */
        free(entries[0]);
        memmove(entries, entries + 1, (HISTORY_MAX - 1) * sizeof(char *));
        entries[HISTORY_MAX - 1] = strdup(line);
    }
}

void history_print(void) {
    for (int i = 0; i < count; i++)
        printf("  %3d  %s\n", i + 1, entries[i]);
}

/* Returns entry at 1-based index, or NULL if out of range */
char *history_get(int index) {
    if (index < 1 || index > count)
        return NULL;
    return entries[index - 1];
}

int history_count(void) {
    return count;
}

void history_free(void) {
    for (int i = 0; i < count; i++) {
        free(entries[i]);
        entries[i] = NULL;
    }
    count = 0;
}
