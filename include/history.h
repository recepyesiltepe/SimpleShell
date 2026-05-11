#ifndef HISTORY_H
#define HISTORY_H

int history_add(const char *line);
int history_print(void);
int history_print_last(int count);
int history_clear(void);
const char *history_get_last(void);
const char *history_get_by_number(int entry_number);
const char *history_find_latest_containing(const char *query);
const char *history_find_previous_containing(const char *query, int before_entry_number,
                                             int *found_entry_number);
int history_get_count(void);
int history_init(void);
void history_cleanup(void);

#endif
