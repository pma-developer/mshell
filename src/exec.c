#include "exec.h"
#include "terminal.h"
#include "vec.h"

#include <errno.h>
#include <string.h>
#include <sys/epoll.h>

/* TODO: make PipeParams struct; replace last 4 arguments with 'const PipeParams *pipe_params' */
Process execCommand(const StrVec *command, pid_t pgid, bool set_pg_to_fg, int epoll_fd, int stdin_fd, int stdout_fd, int *fds_to_close,
                    size_t fds_count) {
    if (set_pg_to_fg) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, STDIN_FILENO, NULL);
    }

    int pid = fork();
    if (pid == -1) {
        perror("fork()");
        return (Process){0};
    }
    if (pid == 0) {

        int setpgid_res = setpgid(pid, pgid);
        int tcsetpgrp_res = 0;
        if (set_pg_to_fg) {
            tcsetpgrp_res = tcsetpgrp(STDIN_FILENO, getpgid(pid));
        }

        /* Resetting termios */
        terminalResetOriginal();
        terminalIgnoreStopSignals(false);

        if (setpgid_res != 0) {
            perror("setpgid");
        }
        if (tcsetpgrp_res != 0) {
            perror("tcsetpgrp(STDIN_FILENO, getpgid(pid));");
        }

        LOG("configuring pipe\n");
        if (stdin_fd > 0) {
            dup2(stdin_fd, STDIN_FILENO);
        }
        if (stdout_fd > 0) {
            dup2(stdout_fd, STDOUT_FILENO);
        }
        for (size_t i = 0; i < fds_count; ++i) {
            close(fds_to_close[i]);
        }

        int execvp_res = execvp(command->items[0], command->items);
        if (execvp_res == -1) {
            printf("Error executing: \"%s\"\n%s\n", command->items[0], strerror(errno));
            // log("error executing: \"%s\"\n", command->items[0]);
        }
        exit(EXIT_FAILURE);
    }

    Process process = {0};
    process.pid = pid;
    process.name = cloneStr(command->items[0]);
    if (process.name == NULL) {
        return (Process){0};
    }
    LOG("allocated process.name %s(%p)", process.name, process.name);
    process.status = PROC_STATUS_RUNNING;
    return process;
}

Job pipeCommands(const StrArrayVec *commandsDa, int epoll_fd) {
    size_t pipefds_count = 2 * (commandsDa->count - 1);
    int pipefds[pipefds_count];
    for (size_t i = 0; i < pipefds_count; i += 2) {
        if (pipe(pipefds + i)) {
            perror("pipe(pipefds)");
            return (Job){0};
        }
    }
    Job pipe_job = {0};
    Process first_proc = execCommand(&commandsDa->items[0], 0, true, epoll_fd, 0, pipefds[PIPE_WRITE], pipefds, pipefds_count);
    if (first_proc.pid == 0) {
        return (Job){0};
    }
    vecAppend(&pipe_job.pipeline, first_proc);
    pipe_job.pgid = first_proc.pid;

    for (size_t i = 1; i < commandsDa->count - 1; ++i) {
        size_t fd_i = 2 * (i - 1);
        Process proc = execCommand(&commandsDa->items[i], first_proc.pid, false, epoll_fd,pipefds[fd_i], pipefds[fd_i + 3],
                                   pipefds, pipefds_count);
        vecAppend(&pipe_job.pipeline, proc);
    }

    Process last_proc = execCommand(&commandsDa->items[commandsDa->count - 1], first_proc.pid, false, epoll_fd,
                                    pipefds[pipefds_count - 2], 0, pipefds, pipefds_count);
    vecAppend(&pipe_job.pipeline, last_proc);

    for (size_t i = 0; i < pipefds_count; ++i) {
        close(pipefds[i]);
    }

    return pipe_job;
}
