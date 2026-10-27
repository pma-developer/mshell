#pragma once

#include "proc.h"
#include "util.h"

#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>

typedef enum PipeFdType {
    PIPE_READ = 0,
    PIPE_WRITE = 1,
} PipeFdType;

void parsePipeline(char *line_buf, StrArrayVec *out);
Process execCommand(const StrVec *command, pid_t pgid, bool set_pg_to_fg, int epoll_fd, int stdin_fd, int stdout_fd,
    int *fds_to_close, size_t fds_count);
Job pipeCommands(const StrArrayVec *commandsDa, int epoll_fd);
