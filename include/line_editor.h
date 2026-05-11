#ifndef LINE_EDITOR_H
#define LINE_EDITOR_H

#include <stddef.h>

int read_command_line(const char *prompt, char *buffer, size_t buffer_size);

#endif
