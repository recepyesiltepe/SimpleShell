#ifndef ALIASES_H
#define ALIASES_H

int aliases_set(const char *name, const char *value);
int aliases_remove(const char *name);
int aliases_print(const char *name);
int aliases_print_all(void);
char *aliases_expand_line(const char *line);
void aliases_cleanup(void);

#endif
