#ifndef HISTORY_H
#define HISTORY_H

int history_add(const char *line);
int history_print(void);
void history_cleanup(void);

#endif
