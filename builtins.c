#include "builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool is_builtin(const Command *cmd) {
    if (!cmd || cmd->argc == 0) {
        return false;
    }
    return strcmp(cmd->argv[0], "cd") == 0 || strcmp(cmd->argv[0], "exit") == 0;
}

static int parse_exit_code(Command *cmd) {
    if (cmd->argc < 2) {
        return 0;
    }
    return atoi(cmd->argv[1]);
}

int run_builtin_parent(Command *cmd, bool *should_exit) {
    *should_exit = false;

    if (strcmp(cmd->argv[0], "cd") == 0) {
        const char *path = cmd->argc > 1 ? cmd->argv[1] : getenv("HOME");
        if (!path) {
            fprintf(stderr, "cd: HOME not set\n");
            return 1;
        }
        if (chdir(path) != 0) {
            perror("cd");
            return 1;
        }
        return 0;
    }

    if (strcmp(cmd->argv[0], "exit") == 0) {
        *should_exit = true;
        return parse_exit_code(cmd);
    }

    return 1;
}

int run_builtin_child(Command *cmd) {
    bool should_exit = false;
    int status = run_builtin_parent(cmd, &should_exit);
    (void)should_exit;
    return status;
}
