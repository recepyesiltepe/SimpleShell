#define _POSIX_C_SOURCE 200809L

#include "execute.h"

#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "builtins.h"
#include "jobs.h"
#include "memory.h"
#include "redirection.h"

typedef struct {
    char *name;
    char *value;
    bool existed;
} SavedEnv;

static char *assignment_name(const char *assignment) {
    const char *equals = strchr(assignment, '=');
    if (!equals) {
        return NULL;
    }
    size_t name_len = (size_t)(equals - assignment);
    char *name = xmalloc(name_len + 1);
    memcpy(name, assignment, name_len);
    name[name_len] = '\0';
    return name;
}

static int set_command_env_permanent(Command *cmd) {
    for (int i = 0; i < cmd->env_count; i++) {
        char *name = assignment_name(cmd->env_assignments[i]);
        if (!name) {
            return 1;
        }
        const char *value = strchr(cmd->env_assignments[i], '=') + 1;
        if (setenv(name, value, 1) != 0) {
            perror("setenv");
            free(name);
            return 1;
        }
        free(name);
    }
    return 0;
}

static int apply_command_env(Command *cmd, SavedEnv **out_saved) {
    *out_saved = NULL;
    if (cmd->env_count == 0) {
        return 0;
    }

    SavedEnv *saved = xmalloc((size_t)cmd->env_count * sizeof(SavedEnv));
    for (int i = 0; i < cmd->env_count; i++) {
        saved[i].name = assignment_name(cmd->env_assignments[i]);
        saved[i].value = NULL;
        saved[i].existed = false;

        if (!saved[i].name) {
            free(saved);
            return 1;
        }

        const char *old_value = getenv(saved[i].name);
        if (old_value) {
            saved[i].value = xstrdup(old_value);
            saved[i].existed = true;
        }

        const char *new_value = strchr(cmd->env_assignments[i], '=') + 1;
        if (setenv(saved[i].name, new_value, 1) != 0) {
            perror("setenv");
            for (int j = 0; j <= i; j++) {
                free(saved[j].name);
                free(saved[j].value);
            }
            free(saved);
            return 1;
        }
    }

    *out_saved = saved;
    return 0;
}

static void restore_command_env(SavedEnv *saved, int count) {
    if (!saved) {
        return;
    }
    for (int i = count - 1; i >= 0; i--) {
        if (saved[i].existed) {
            if (setenv(saved[i].name, saved[i].value, 1) != 0) {
                perror("setenv");
            }
        } else if (unsetenv(saved[i].name) != 0) {
            perror("unsetenv");
        }
        free(saved[i].name);
        free(saved[i].value);
    }
    free(saved);
}

static int save_and_apply_parent_redirections(Command *cmd, int *saved_stdin, int *saved_stdout) {
    *saved_stdin = -1;
    *saved_stdout = -1;

    if (cmd->input_file) {
        *saved_stdin = dup(STDIN_FILENO);
        if (*saved_stdin < 0) {
            perror("dup");
            return 1;
        }
    }
    if (cmd->output_file) {
        *saved_stdout = dup(STDOUT_FILENO);
        if (*saved_stdout < 0) {
            perror("dup");
            if (*saved_stdin >= 0) {
                close(*saved_stdin);
                *saved_stdin = -1;
            }
            return 1;
        }
    }

    if (apply_redirections(cmd) != 0) {
        if (*saved_stdin >= 0) {
            dup2(*saved_stdin, STDIN_FILENO);
            close(*saved_stdin);
            *saved_stdin = -1;
        }
        if (*saved_stdout >= 0) {
            dup2(*saved_stdout, STDOUT_FILENO);
            close(*saved_stdout);
            *saved_stdout = -1;
        }
        return 1;
    }
    return 0;
}

static void restore_parent_redirections(int saved_stdin, int saved_stdout) {
    fflush(stdout);
    if (saved_stdin >= 0) {
        if (dup2(saved_stdin, STDIN_FILENO) < 0) {
            perror("dup2");
        }
        close(saved_stdin);
    }
    if (saved_stdout >= 0) {
        if (dup2(saved_stdout, STDOUT_FILENO) < 0) {
            perror("dup2");
        }
        close(saved_stdout);
    }
}

static void close_pipe_ends(int (*pipes)[2], int pipe_count) {
    for (int i = 0; i < pipe_count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

static int status_from_wait(int wait_status) {
    if (WIFEXITED(wait_status)) {
        return WEXITSTATUS(wait_status);
    }
    if (WIFSIGNALED(wait_status)) {
        return 128 + WTERMSIG(wait_status);
    }
    return 1;
}

static int run_pipeline(Pipeline *pipeline, bool *should_exit) {
    *should_exit = false;

    if (pipeline->count == 1 && pipeline->commands[0].argc == 0) {
        return set_command_env_permanent(&pipeline->commands[0]);
    }

    if (pipeline->count == 1 && is_builtin(&pipeline->commands[0])) {
        Command *cmd = &pipeline->commands[0];
        int saved_stdin = -1;
        int saved_stdout = -1;
        if (save_and_apply_parent_redirections(cmd, &saved_stdin, &saved_stdout) != 0) {
            return 1;
        }

        SavedEnv *saved_env = NULL;
        if (apply_command_env(cmd, &saved_env) != 0) {
            restore_parent_redirections(saved_stdin, saved_stdout);
            return 1;
        }

        int status = run_builtin_parent(cmd, should_exit);
        restore_command_env(saved_env, cmd->env_count);
        restore_parent_redirections(saved_stdin, saved_stdout);
        return status;
    }

    int pipe_count = pipeline->count - 1;
    int (*pipes)[2] = NULL;
    if (pipe_count > 0) {
        pipes = xmalloc((size_t)pipe_count * sizeof(int[2]));
        for (int i = 0; i < pipe_count; i++) {
            if (pipe(pipes[i]) < 0) {
                perror("pipe");
                for (int j = 0; j < i; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
                free(pipes);
                return 1;
            }
        }
    }

    pid_t *pids = xmalloc((size_t)pipeline->count * sizeof(pid_t));

    for (int i = 0; i < pipeline->count; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            if (pipes) {
                close_pipe_ends(pipes, pipe_count);
            }
            free(pipes);
            free(pids);
            return 1;
        }

        if (pid == 0) {
            if (set_command_env_permanent(&pipeline->commands[i]) != 0) {
                _exit(EXIT_FAILURE);
            }
            if (pipe_count > 0) {
                if (i > 0 && dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                    perror("dup2");
                    _exit(EXIT_FAILURE);
                }
                if (i < pipeline->count - 1 && dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    perror("dup2");
                    _exit(EXIT_FAILURE);
                }
                close_pipe_ends(pipes, pipe_count);
            }

            if (apply_redirections(&pipeline->commands[i]) != 0) {
                _exit(EXIT_FAILURE);
            }

            if (pipeline->commands[i].argc == 0) {
                _exit(EXIT_SUCCESS);
            }

            if (is_builtin(&pipeline->commands[i])) {
                int status = run_builtin_child(&pipeline->commands[i]);
                _exit(status);
            }

            execvp(pipeline->commands[i].argv[0], pipeline->commands[i].argv);
            fprintf(stderr, "%s: %s\n", pipeline->commands[i].argv[0], strerror(errno));
            _exit(EXIT_FAILURE);
        }

        pids[i] = pid;
    }

    if (pipes) {
        close_pipe_ends(pipes, pipe_count);
        free(pipes);
    }

    int final_status = 0;
    for (int i = 0; i < pipeline->count; i++) {
        int wait_status = 0;
        waitpid(pids[i], &wait_status, 0);
        if (i == pipeline->count - 1) {
            final_status = status_from_wait(wait_status);
        }
    }

    free(pids);
    return final_status;
}

static int append_text(char *buffer, size_t buffer_size, size_t *index, const char *text) {
    while (*text != '\0') {
        if (*index + 1 >= buffer_size) {
            return -1;
        }
        buffer[(*index)++] = *text++;
    }
    return 0;
}

static int describe_pipeline(Pipeline *pipeline, char *buffer, size_t buffer_size, size_t *index) {
    for (int i = 0; i < pipeline->count; i++) {
        Command *cmd = &pipeline->commands[i];
        for (int j = 0; j < cmd->env_count; j++) {
            if (*index > 0 && append_text(buffer, buffer_size, index, " ") != 0) {
                return -1;
            }
            if (append_text(buffer, buffer_size, index, cmd->env_assignments[j]) != 0) {
                return -1;
            }
        }
        for (int j = 0; j < cmd->argc; j++) {
            if (*index > 0 && append_text(buffer, buffer_size, index, " ") != 0) {
                return -1;
            }
            if (append_text(buffer, buffer_size, index, cmd->argv[j]) != 0) {
                return -1;
            }
        }
        if (i < pipeline->count - 1 && append_text(buffer, buffer_size, index, " |") != 0) {
            return -1;
        }
    }
    return 0;
}

static int describe_ast(AstNode *node, char *buffer, size_t buffer_size, size_t *index) {
    if (!node) {
        return 0;
    }
    switch (node->type) {
    case AST_NODE_PIPELINE:
        return describe_pipeline(&node->pipeline, buffer, buffer_size, index);
    case AST_NODE_AND:
        if (describe_ast(node->left, buffer, buffer_size, index) != 0 ||
            append_text(buffer, buffer_size, index, " && ") != 0 ||
            describe_ast(node->right, buffer, buffer_size, index) != 0) {
            return -1;
        }
        return 0;
    case AST_NODE_OR:
        if (describe_ast(node->left, buffer, buffer_size, index) != 0 ||
            append_text(buffer, buffer_size, index, " || ") != 0 ||
            describe_ast(node->right, buffer, buffer_size, index) != 0) {
            return -1;
        }
        return 0;
    case AST_NODE_SEQUENCE:
        if (describe_ast(node->left, buffer, buffer_size, index) != 0 ||
            append_text(buffer, buffer_size, index, " ; ") != 0 ||
            describe_ast(node->right, buffer, buffer_size, index) != 0) {
            return -1;
        }
        return 0;
    case AST_NODE_BACKGROUND:
        if (describe_ast(node->left, buffer, buffer_size, index) != 0 ||
            append_text(buffer, buffer_size, index, " &") != 0) {
            return -1;
        }
        if (node->right) {
            if (append_text(buffer, buffer_size, index, " ") != 0 ||
                describe_ast(node->right, buffer, buffer_size, index) != 0) {
                return -1;
            }
        }
        return 0;
    }
    return -1;
}

static int run_background(AstNode *node) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        bool should_exit = false;
        int status = execute_ast(node->left, &should_exit);
        _exit(status);
    }

    char description[512];
    size_t index = 0;
    if (describe_ast(node->left, description, sizeof(description), &index) != 0) {
        snprintf(description, sizeof(description), "background-job-%d", pid);
    } else {
        description[index] = '\0';
    }

    int job_id = jobs_add(pid, description);
    printf("[%d] %d\n", job_id, pid);
    return 0;
}

int execute_ast(AstNode *node, bool *should_exit) {
    if (!node) {
        *should_exit = false;
        return 0;
    }

    switch (node->type) {
    case AST_NODE_PIPELINE:
        return run_pipeline(&node->pipeline, should_exit);

    case AST_NODE_SEQUENCE: {
        int left_status = execute_ast(node->left, should_exit);
        if (*should_exit) {
            return left_status;
        }
        return execute_ast(node->right, should_exit);
    }

    case AST_NODE_AND: {
        int left_status = execute_ast(node->left, should_exit);
        if (*should_exit || left_status != 0) {
            return left_status;
        }
        return execute_ast(node->right, should_exit);
    }

    case AST_NODE_OR: {
        int left_status = execute_ast(node->left, should_exit);
        if (*should_exit || left_status == 0) {
            return left_status;
        }
        return execute_ast(node->right, should_exit);
    }

    case AST_NODE_BACKGROUND:
        *should_exit = false;
        return run_background(node);
    }

    *should_exit = false;
    return 1;
}
