#ifndef HISTORY_H
#define HISTORY_H

#define HISTORY_MAX 100

void  history_init(void);
void  history_add(const char *line);
void  history_print(void);
char *history_get(int index);   /* 1-based */
int   history_count(void);
void  history_free(void);

#endif
