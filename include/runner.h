#ifndef RUNNER_H
#define RUNNER_H

#include <stdbool.h>

int run_shell_line(const char *line, int *exit_status, bool *should_exit);
int run_shell_file(const char *path, int *exit_status, bool *should_exit);

#endif
