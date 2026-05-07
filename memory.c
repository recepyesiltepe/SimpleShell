#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    void *resized = realloc(ptr, size);
    if (!resized) {
        perror("realloc");
        exit(EXIT_FAILURE);
    }
    return resized;
}

char *xstrdup(const char *source) {
    size_t len = strlen(source);
    char *copy = xmalloc(len + 1);
    memcpy(copy, source, len + 1);
    return copy;
}
