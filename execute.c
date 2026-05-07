#include "execute.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "builtins.h"
#include "memory.h"
#include "redirection.h"

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

    if (pipeline->count == 1 && is_builtin(&pipeline->commands[0])) {
        return run_builtin_parent(&pipeline->commands[0], should_exit);
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
    }

    *should_exit = false;
    return 1;
}
