#define _POSIX_C_SOURCE 200809L

#include "builtins.h"

#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "history.h"
#include "jobs.h"

bool is_builtin(const Command *cmd) {
    if (!cmd || cmd->argc == 0) {
        return false;
    }
    return strcmp(cmd->argv[0], "cd") == 0 || strcmp(cmd->argv[0], "exit") == 0 ||
           strcmp(cmd->argv[0], "jobs") == 0 || strcmp(cmd->argv[0], "fg") == 0 ||
           strcmp(cmd->argv[0], "bg") == 0 || strcmp(cmd->argv[0], "pwd") == 0 ||
           strcmp(cmd->argv[0], "export") == 0 || strcmp(cmd->argv[0], "unset") == 0 ||
           strcmp(cmd->argv[0], "history") == 0 || strcmp(cmd->argv[0], "help") == 0;
}

static int parse_exit_code(Command *cmd) {
    if (cmd->argc < 2) {
        return 0;
    }
    return atoi(cmd->argv[1]);
}

static int parse_requested_job_id(const Command *cmd, const char *builtin_name, int *requested_id) {
    if (cmd->argc <= 1) {
        *requested_id = -1;
        return 0;
    }

    const char *id_text = cmd->argv[1];
    if (id_text[0] == '%') {
        id_text++;
    }
    if (id_text[0] == '\0') {
        fprintf(stderr, "%s: invalid job id\n", builtin_name);
        return 1;
    }

    char *end = NULL;
    long parsed = strtol(id_text, &end, 10);
    if (*end != '\0' || parsed <= 0 || parsed > INT_MAX) {
        fprintf(stderr, "%s: invalid job id\n", builtin_name);
        return 1;
    }

    *requested_id = (int)parsed;
    return 0;
}

static bool is_valid_name_char(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
           ch == '_';
}

static bool is_valid_env_name(const char *name) {
    if (!name || name[0] == '\0') {
        return false;
    }
    if (!((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z') ||
          name[0] == '_')) {
        return false;
    }
    for (const char *cursor = name + 1; *cursor != '\0'; cursor++) {
        if (!is_valid_name_char(*cursor)) {
            return false;
        }
    }
    return true;
}

static int run_pwd_builtin(Command *cmd) {
    if (cmd->argc > 1) {
        fprintf(stderr, "pwd: too many arguments\n");
        return 1;
    }

    char *cwd = getcwd(NULL, 0);
    if (!cwd) {
        perror("pwd");
        return 1;
    }

    printf("%s\n", cwd);
    free(cwd);
    return 0;
}

static int run_help_builtin(Command *cmd) {
    if (cmd->argc > 1) {
        fprintf(stderr, "help: too many arguments\n");
        return 1;
    }

    printf("Builtins:\n");
    printf("  cd [DIR|-]          Change directory\n");
    printf("  pwd                 Print current directory\n");
    printf("  export KEY=VALUE    Set environment variable\n");
    printf("  unset NAME...       Unset environment variable(s)\n");
    printf("  history [N|-c]      Show last N history entries or clear history\n");
    printf("  jobs                List background jobs\n");
    printf("  fg [%%JOB]           Bring job to foreground\n");
    printf("  bg [%%JOB]           Resume job in background\n");
    printf("  exit [STATUS]       Exit shell\n");
    printf("  help                Show this message\n");
    printf("History expansion:\n");
    printf("  !!                  Repeat previous command\n");
    printf("  !N                  Run history entry number N\n");
    return 0;
}

static int run_export_builtin(Command *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "export: expected KEY=VALUE\n");
        return 1;
    }

    for (int i = 1; i < cmd->argc; i++) {
        char *assignment = cmd->argv[i];
        char *equals = strchr(assignment, '=');
        if (!equals || equals == assignment) {
            fprintf(stderr, "export: invalid assignment: %s\n", assignment);
            return 1;
        }

        size_t name_len = (size_t)(equals - assignment);
        char *name = malloc(name_len + 1);
        if (!name) {
            perror("export");
            return 1;
        }
        memcpy(name, assignment, name_len);
        name[name_len] = '\0';

        if (!is_valid_env_name(name)) {
            fprintf(stderr, "export: invalid variable name: %s\n", name);
            free(name);
            return 1;
        }

        if (setenv(name, equals + 1, 1) != 0) {
            perror("export");
            free(name);
            return 1;
        }

        free(name);
    }

    return 0;
}

static int run_unset_builtin(Command *cmd) {
    if (cmd->argc < 2) {
        fprintf(stderr, "unset: expected one or more variable names\n");
        return 1;
    }

    for (int i = 1; i < cmd->argc; i++) {
        const char *name = cmd->argv[i];
        if (!is_valid_env_name(name)) {
            fprintf(stderr, "unset: invalid variable name: %s\n", name);
            return 1;
        }

        if (unsetenv(name) != 0) {
            perror("unset");
            return 1;
        }
    }

    return 0;
}

int run_builtin_parent(Command *cmd, bool *should_exit) {
    *should_exit = false;

    if (strcmp(cmd->argv[0], "cd") == 0) {
        if (cmd->argc > 2) {
            fprintf(stderr, "cd: too many arguments\n");
            return 1;
        }

        const char *path = cmd->argc > 1 ? cmd->argv[1] : getenv("HOME");
        const char *resolved_path = path;
        if (!path) {
            fprintf(stderr, "cd: HOME not set\n");
            return 1;
        }

        if (strcmp(path, "-") == 0) {
            const char *oldpwd = getenv("OLDPWD");
            if (!oldpwd) {
                fprintf(stderr, "cd: OLDPWD not set\n");
                return 1;
            }
            resolved_path = oldpwd;
        }

        char *previous_dir = getcwd(NULL, 0);
        if (!previous_dir) {
            perror("cd");
            return 1;
        }

        if (chdir(resolved_path) != 0) {
            perror("cd");
            free(previous_dir);
            return 1;
        }

        if (setenv("OLDPWD", previous_dir, 1) != 0) {
            perror("cd");
            free(previous_dir);
            return 1;
        }
        free(previous_dir);

        char *current_dir = getcwd(NULL, 0);
        if (!current_dir) {
            perror("cd");
            return 1;
        }
        if (setenv("PWD", current_dir, 1) != 0) {
            perror("cd");
            free(current_dir);
            return 1;
        }
        if (strcmp(path, "-") == 0) {
            printf("%s\n", current_dir);
        }
        free(current_dir);
        return 0;
    }

    if (strcmp(cmd->argv[0], "exit") == 0) {
        *should_exit = true;
        return parse_exit_code(cmd);
    }

    if (strcmp(cmd->argv[0], "jobs") == 0) {
        return jobs_print();
    }

    if (strcmp(cmd->argv[0], "fg") == 0) {
        int requested_id = 0;
        if (parse_requested_job_id(cmd, "fg", &requested_id) != 0) {
            return 1;
        }

        int fg_status = 0;
        if (jobs_foreground(requested_id, &fg_status) != 0) {
            return 1;
        }
        return fg_status;
    }

    if (strcmp(cmd->argv[0], "bg") == 0) {
        int requested_id = 0;
        if (parse_requested_job_id(cmd, "bg", &requested_id) != 0) {
            return 1;
        }

        return jobs_background(requested_id);
    }

    if (strcmp(cmd->argv[0], "pwd") == 0) {
        return run_pwd_builtin(cmd);
    }

    if (strcmp(cmd->argv[0], "export") == 0) {
        return run_export_builtin(cmd);
    }

    if (strcmp(cmd->argv[0], "unset") == 0) {
        return run_unset_builtin(cmd);
    }

    if (strcmp(cmd->argv[0], "history") == 0) {
        if (cmd->argc == 1) {
            return history_print();
        }
        if (cmd->argc > 2) {
            fprintf(stderr, "history: usage: history [N|-c]\n");
            return 1;
        }

        if (strcmp(cmd->argv[1], "-c") == 0) {
            return history_clear();
        }

        char *end = NULL;
        errno = 0;
        long count = strtol(cmd->argv[1], &end, 10);
        if (errno != 0 || *end != '\0' || count < 0 || count > INT_MAX) {
            fprintf(stderr, "history: invalid count: %s\n", cmd->argv[1]);
            return 1;
        }
        return history_print_last((int)count);
    }

    if (strcmp(cmd->argv[0], "help") == 0) {
        return run_help_builtin(cmd);
    }

    return 1;
}

int run_builtin_child(Command *cmd) {
    if (strcmp(cmd->argv[0], "jobs") == 0 || strcmp(cmd->argv[0], "fg") == 0 ||
        strcmp(cmd->argv[0], "bg") == 0) {
        fprintf(stderr, "%s: not supported in pipeline\n", cmd->argv[0]);
        return 1;
    }
    bool should_exit = false;
    int status = run_builtin_parent(cmd, &should_exit);
    (void)should_exit;
    return status;
}

void builtins_reap_jobs(void) {
    jobs_reap_finished();
}
