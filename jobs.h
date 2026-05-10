#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>

void jobs_reap_finished(void);
int jobs_add(pid_t pid, const char *command);
int jobs_print(void);
int jobs_foreground(int requested_id, int *exit_status);
int jobs_background(int requested_id);
void jobs_cleanup(void);

#endif
