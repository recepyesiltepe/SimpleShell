#include "redirection.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int apply_redirections(Command *cmd) {
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
