#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LINE_LEN 4096

static char *xstrdup(const char *source) {
    size_t len = strlen(source);
    char *copy = malloc(len + 1);
    if (!copy) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    memcpy(copy, source, len + 1);
    return copy;
}

typedef struct {
    char **items;
    int count;
    int capacity;
} TokenList;

typedef struct {
    char **argv;
    int argc;
    int argv_capacity;
    char *input_file;
    char *output_file;
    bool append_output;
} Command;

typedef struct {
    Command *commands;
    int count;
    int capacity;
} Pipeline;

static void token_list_init(TokenList *list) {
    list->count = 0;
    list->capacity = 16;
    list->items = malloc((size_t)list->capacity * sizeof(char *));
    if (!list->items) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
}

static void token_list_push(TokenList *list, char *token) {
    if (list->count == list->capacity) {
        list->capacity *= 2;
        char **resized = realloc(list->items, (size_t)list->capacity * sizeof(char *));
        if (!resized) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        list->items = resized;
    }
    list->items[list->count++] = token;
}

static void token_list_free(TokenList *list) {
    for (int i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void command_init(Command *cmd) {
    cmd->argc = 0;
    cmd->argv_capacity = 8;
    cmd->argv = malloc((size_t)cmd->argv_capacity * sizeof(char *));
    if (!cmd->argv) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->append_output = false;
}

static void command_add_arg(Command *cmd, const char *arg) {
    if (cmd->argc + 1 >= cmd->argv_capacity) {
        cmd->argv_capacity *= 2;
        char **resized = realloc(cmd->argv, (size_t)cmd->argv_capacity * sizeof(char *));
        if (!resized) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        cmd->argv = resized;
    }
    cmd->argv[cmd->argc] = xstrdup(arg);
    if (!cmd->argv[cmd->argc]) {
        perror("strdup");
        exit(EXIT_FAILURE);
    }
    cmd->argc++;
    cmd->argv[cmd->argc] = NULL;
}

static void command_free(Command *cmd) {
    for (int i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
    }
    free(cmd->argv);
    free(cmd->input_file);
    free(cmd->output_file);
    cmd->argv = NULL;
    cmd->argc = 0;
    cmd->argv_capacity = 0;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
}

static void pipeline_init(Pipeline *pipeline) {
    pipeline->count = 0;
    pipeline->capacity = 4;
    pipeline->commands = malloc((size_t)pipeline->capacity * sizeof(Command));
    if (!pipeline->commands) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
}

static void pipeline_push_command(Pipeline *pipeline, Command *cmd) {
    if (pipeline->count == pipeline->capacity) {
        pipeline->capacity *= 2;
        Command *resized = realloc(pipeline->commands, (size_t)pipeline->capacity * sizeof(Command));
        if (!resized) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        pipeline->commands = resized;
    }
    pipeline->commands[pipeline->count++] = *cmd;
}

static void pipeline_free(Pipeline *pipeline) {
    for (int i = 0; i < pipeline->count; i++) {
        command_free(&pipeline->commands[i]);
    }
    free(pipeline->commands);
    pipeline->commands = NULL;
    pipeline->count = 0;
    pipeline->capacity = 0;
}

static int tokenize_line(const char *line, TokenList *tokens) {
    const char *cursor = line;
    while (*cursor != '\0') {
        while (isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        if (*cursor == '|') {
            token_list_push(tokens, xstrdup("|"));
            cursor++;
            continue;
        }

        if (*cursor == '<') {
            token_list_push(tokens, xstrdup("<"));
            cursor++;
            continue;
        }

        if (*cursor == '>') {
            if (*(cursor + 1) == '>') {
                token_list_push(tokens, xstrdup(">>"));
                cursor += 2;
            } else {
                token_list_push(tokens, xstrdup(">"));
                cursor++;
            }
            continue;
        }

        char buffer[MAX_LINE_LEN];
        size_t index = 0;

        while (*cursor != '\0' && !isspace((unsigned char)*cursor) &&
               *cursor != '|' && *cursor != '<' && *cursor != '>') {
            if (*cursor == '\'' || *cursor == '"') {
                char quote = *cursor;
                cursor++;
                while (*cursor != '\0' && *cursor != quote) {
                    if (index + 1 >= sizeof(buffer)) {
                        fprintf(stderr, "Error: token too long\n");
                        return -1;
                    }
                    buffer[index++] = *cursor++;
                }
                if (*cursor != quote) {
                    fprintf(stderr, "Error: unmatched quote\n");
                    return -1;
                }
                cursor++;
            } else {
                if (index + 1 >= sizeof(buffer)) {
                    fprintf(stderr, "Error: token too long\n");
                    return -1;
                }
                buffer[index++] = *cursor++;
            }
        }

        buffer[index] = '\0';
        token_list_push(tokens, xstrdup(buffer));
    }
    return 0;
}

static int parse_tokens(TokenList *tokens, Pipeline *pipeline) {
    if (tokens->count == 0) {
        return 1;
    }

    Command current;
    command_init(&current);

    for (int i = 0; i < tokens->count; i++) {
        const char *token = tokens->items[i];

        if (strcmp(token, "|") == 0) {
            if (current.argc == 0) {
                fprintf(stderr, "Syntax error: missing command near '|'\n");
                command_free(&current);
                return -1;
            }
            pipeline_push_command(pipeline, &current);
            command_init(&current);
            continue;
        }

        if (strcmp(token, "<") == 0) {
            if (i + 1 >= tokens->count) {
                fprintf(stderr, "Syntax error: expected file after '<'\n");
                command_free(&current);
                return -1;
            }
            i++;
            free(current.input_file);
            current.input_file = xstrdup(tokens->items[i]);
            if (!current.input_file) {
                perror("strdup");
                command_free(&current);
                return -1;
            }
            continue;
        }

        if (strcmp(token, ">") == 0 || strcmp(token, ">>") == 0) {
            if (i + 1 >= tokens->count) {
                fprintf(stderr, "Syntax error: expected file after '%s'\n", token);
                command_free(&current);
                return -1;
            }
            i++;
            free(current.output_file);
            current.output_file = xstrdup(tokens->items[i]);
            if (!current.output_file) {
                perror("strdup");
                command_free(&current);
                return -1;
            }
            current.append_output = (strcmp(token, ">>") == 0);
            continue;
        }

        command_add_arg(&current, token);
    }

    if (current.argc == 0) {
        fprintf(stderr, "Syntax error: trailing pipe or redirection\n");
        command_free(&current);
        return -1;
    }

    pipeline_push_command(pipeline, &current);
    return 0;
}

static int run_builtin(Command *cmd) {
    if (cmd->argc == 0) {
        return 1;
    }

    if (strcmp(cmd->argv[0], "cd") == 0) {
        const char *path = cmd->argc > 1 ? cmd->argv[1] : getenv("HOME");
        if (!path) {
            fprintf(stderr, "cd: HOME not set\n");
            return 1;
        }
        if (chdir(path) != 0) {
            perror("cd");
        }
        return 1;
    }

    if (strcmp(cmd->argv[0], "exit") == 0) {
        exit(0);
    }

    return 0;
}

static int apply_redirections(Command *cmd) {
    if (cmd->input_file) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) {
            perror(cmd->input_file);
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }

    if (cmd->output_file) {
        int flags = O_WRONLY | O_CREAT;
        flags |= cmd->append_output ? O_APPEND : O_TRUNC;
        int fd = open(cmd->output_file, flags, 0644);
        if (fd < 0) {
            perror(cmd->output_file);
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }

    return 0;
}

static void close_pipe_ends(int (*pipes)[2], int pipe_count) {
    for (int i = 0; i < pipe_count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

static int execute_pipeline(Pipeline *pipeline) {
    if (pipeline->count == 1 && run_builtin(&pipeline->commands[0])) {
        return 0;
    }

    int pipe_count = pipeline->count - 1;
    int (*pipes)[2] = NULL;

    if (pipe_count > 0) {
        pipes = malloc((size_t)pipe_count * sizeof(int[2]));
        if (!pipes) {
            perror("malloc");
            return -1;
        }
        for (int i = 0; i < pipe_count; i++) {
            if (pipe(pipes[i]) < 0) {
                perror("pipe");
                for (int j = 0; j < i; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
                free(pipes);
                return -1;
            }
        }
    }

    pid_t *pids = malloc((size_t)pipeline->count * sizeof(pid_t));
    if (!pids) {
        perror("malloc");
        free(pipes);
        return -1;
    }

    for (int i = 0; i < pipeline->count; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            if (pipes) {
                close_pipe_ends(pipes, pipe_count);
            }
            free(pids);
            free(pipes);
            return -1;
        }

        if (pid == 0) {
            if (pipe_count > 0) {
                if (i > 0) {
                    if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                        perror("dup2");
                        _exit(EXIT_FAILURE);
                    }
                }
                if (i < pipeline->count - 1) {
                    if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                        perror("dup2");
                        _exit(EXIT_FAILURE);
                    }
                }
                close_pipe_ends(pipes, pipe_count);
            }

            if (apply_redirections(&pipeline->commands[i]) != 0) {
                _exit(EXIT_FAILURE);
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

    for (int i = 0; i < pipeline->count; i++) {
        int status = 0;
        waitpid(pids[i], &status, 0);
    }

    free(pids);
    return 0;
}

int main(void) {
    char line[MAX_LINE_LEN];

    while (1) {
        printf("SimpleShell$ ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            if (feof(stdin)) {
                putchar('\n');
                break;
            }
            perror("fgets");
            continue;
        }

        size_t line_len = strlen(line);
        if (line_len > 0 && line[line_len - 1] == '\n') {
            line[line_len - 1] = '\0';
        }

        TokenList tokens;
        token_list_init(&tokens);
        if (tokenize_line(line, &tokens) != 0) {
            token_list_free(&tokens);
            continue;
        }

        Pipeline pipeline;
        pipeline_init(&pipeline);
        int parse_status = parse_tokens(&tokens, &pipeline);
        if (parse_status == 1) {
            token_list_free(&tokens);
            pipeline_free(&pipeline);
            continue;
        }
        if (parse_status != 0) {
            token_list_free(&tokens);
            pipeline_free(&pipeline);
            continue;
        }

        execute_pipeline(&pipeline);

        pipeline_free(&pipeline);
        token_list_free(&tokens);
    }

    return 0;
}
