#ifndef HISTORY_H
#define HISTORY_H

#define HISTORY_MAX 100

void  history_init(void);
void  history_add(const char *line);
void  history_print(void);
void  history_free(void);

#endif
