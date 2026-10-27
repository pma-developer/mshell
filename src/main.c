//#define ENABLE_LOGS
#include "proc.h"
#include "terminal.h"
#include "util.h"
#include "vec.h"
#include "exec.h"

#include <bits/types/sigset_t.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

const int MAX_STDIN_READ_ERRORS = 5;

#define MAX_EPOLL_EVENTS 10
#define MAX_PROMPT_SUB_STR_SIZE 128
#define PROMPT_SIZE ((MAX_PROMPT_SUB_STR_SIZE * 3) + 50)

void printPrompt() {
    char cwd_cstr[MAX_PROMPT_SUB_STR_SIZE] = {0};
    if (getcwd(cwd_cstr, LENGTH(cwd_cstr)) == NULL) {
        perror("getcwd");
    }
    char hostname_cstr[MAX_PROMPT_SUB_STR_SIZE] = {0};
    if (gethostname(hostname_cstr, LENGTH(hostname_cstr)) < 0) {
        perror("gethostname");
    }
    char username_cstr[MAX_PROMPT_SUB_STR_SIZE] = {0};
    if (getlogin_r(username_cstr, LENGTH(username_cstr)) != 0) {
        perror("getlogin_r");
    }

    printf("[%s@%s %s]$ ", username_cstr, hostname_cstr, cwd_cstr);
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    const size_t max_line_len = 1024;
    struct termios shell_termios;

    if (!terminalInit(&shell_termios)) {
        return EXIT_FAILURE;
    }
    atexit(terminalResetOriginal);
    terminalIgnoreStopSignals(true);

    char line_buf[max_line_len + 1];
    size_t line_len = 0;

    bool line_ended = false;

    JobsVec bg_jobs = {0};
    Job fg_job = {0};

    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &sig_mask, NULL) == -1) {
        perror("sigprocmask");
        exit(EXIT_FAILURE);
    }
    int sig_chld_fd = signalfd(-1, &sig_mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (sig_chld_fd == -1) {
        perror("signalfd");
        exit(EXIT_FAILURE);
    }
    struct epoll_event event_buf[MAX_EPOLL_EVENTS];
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return 1;
    }
    struct epoll_event stdin_epoll_event;
    stdin_epoll_event.data.fd = STDIN_FILENO;
    stdin_epoll_event.events = EPOLLIN;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &stdin_epoll_event);

    struct epoll_event sig_chld_fd_epoll_event;
    sig_chld_fd_epoll_event.data.fd = sig_chld_fd;
    sig_chld_fd_epoll_event.events = EPOLLIN;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sig_chld_fd, &sig_chld_fd_epoll_event);

    ProcStatus fg_status = PROC_STATUS_UNDEFINED;
    printf("mshell v0.0.0 *_*\n\n");
    printPrompt();
    while (true) {

        LOG("starting epoll wait\n");
        const int ready_fds = epoll_wait(epoll_fd, event_buf, MAX_EPOLL_EVENTS, -1);
        if (ready_fds == -1) {
            perror("epoll wait");
        }
        LOG("epoll ready_fds: %i\n", ready_fds);

        for (size_t fd_i = 0; fd_i < ready_fds; ++fd_i) {
            if (event_buf[fd_i].data.fd == sig_chld_fd) {
                struct signalfd_siginfo sig_chld_info;
                ssize_t read_count = 0;
                LOG("SIGCHLD is available for read;\n");

                while ((read_count = read(sig_chld_fd, &sig_chld_info, sizeof(sig_chld_info)))) {
                    LOG("reading sig_chld_fd\n");
                    if (read_count < 0) {
                        if (errno == EAGAIN) {
                            break;
                        }
                        perror("read(sig_chld_fd)");
                        continue;
                    }

                    updateProcStatuses(&fg_job.pipeline);
                    for (size_t i = 0; i < bg_jobs.count; ) {
                        Job *bg_job = &bg_jobs.items[i];

                        bool any_updated = updateProcStatuses(&bg_job->pipeline);
                        ProcStatus bg_job_status = getPipelineStatus(&bg_job->pipeline);

                        if (bg_job_status == PROC_STATUS_TERMINATED) {

                            for (size_t j = 0; j < bg_job->pipeline.count; ++j) {
                                freeProcessFields(&bg_job->pipeline.items[j]);
                            }
                            vecFree(&bg_job->pipeline);
                            vecRemoveAt(&bg_jobs, i);

                            printPrompt();
                            continue;
                        }

                        if (bg_job_status == PROC_STATUS_SUSPENDED && any_updated) {
                            LOG("termios of bg_job(%i) set\n", bg_job->pgid);
                            tcgetattr(STDIN_FILENO, &bg_job->termios_conf);
                            printPrompt();
                        }

                        ++i;
                    }

                    fg_status = getPipelineStatus(&fg_job.pipeline);

                    LOG("fg_status %i\n", fg_status);
                    if (fg_status == PROC_STATUS_SUSPENDED || fg_status == PROC_STATUS_TERMINATED) {
                        if (fg_status == PROC_STATUS_SUSPENDED) {
                            tcgetattr(STDIN_FILENO, &fg_job.termios_conf);
                            vecAppend(&bg_jobs, fg_job);
                        } else if (fg_status == PROC_STATUS_TERMINATED) {
                            for (size_t i = 0; i < fg_job.pipeline.count; ++i) {
                                freeProcessFields(&fg_job.pipeline.items[i]);
                            }
                            vecFree(&fg_job.pipeline);
                        }
                        fg_job = (Job){0};
                        /* return control to the shell */
                        LOG("returning control to the shell\n");
                        if (tcsetpgrp(STDIN_FILENO, getpgid(0)) != 0) {
                            perror("tcsetpgrp(STDIN_FILENO, getpgid(0));");
                        }
                        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &shell_termios) != 0) {
                            perror("tcsetattr(STDIN_FILENO, TCSAFLUSH, &shell_termios)");
                        }
                        printPrompt();
                        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &stdin_epoll_event);
                    }
                }
                LOG("sigchld processed\n");
            } else if (event_buf[fd_i].data.fd == STDIN_FILENO) {
                size_t read_error_count = 0;
                ssize_t read_count = 0;

                LOG("STDIN is available for read;\n");

                while (!line_ended && line_len < max_line_len &&
                       (read_count = read(STDIN_FILENO, line_buf + line_len, 1))) {

                    if (read_count < 0) {
                        LOG("errno: %d\n", errno);
                        perror("Error reading STDIN");
                        read_error_count++;
                        if (read_error_count >= MAX_STDIN_READ_ERRORS) {
                            LOG("Too many STDIN read errors\n");
                            exit(EXIT_FAILURE);
                        }
                        continue;
                    }
                    read_error_count = 0;
                    line_len += read_count;
                    char last_char = line_buf[line_len - 1];

                    if ((unsigned char)last_char == shell_termios.c_cc[VINTR]) {
                        printf("received VINTR\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VQUIT]) {
                        printf("received VQUIT\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VERASE]) {
                        printf("received VERASE\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VKILL]) {
                        printf("received VKILL\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VEOF]) {
                        printf("received VEOF\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VTIME]) {
                        printf("received VTIME\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VMIN]) {
                        printf("received VMIN\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VSWTC]) {
                        printf("received VSWTC\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VSTART]) {
                        printf("received VSTART\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VSTOP]) {
                        printf("received VSTOP\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VSUSP]) {
                        printf("received VSUSP\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VEOL]) {
                        printf("received VEOL\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VREPRINT]) {
                        printf("received VREPRINT\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VDISCARD]) {
                        printf("received VDISCARD\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VWERASE]) {
                        printf("received VWERASE\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VLNEXT]) {
                        printf("received VLNEXT\n");
                    } else if ((unsigned char)last_char == shell_termios.c_cc[VEOL2]) {
                        printf("received VEOL2\n");
                    } else if (last_char == '\n') {
                        line_ended = true;
                    }
                }
                if (line_len >= max_line_len) {
                    printf("exceeded max_line_len(%zu)\n", max_line_len);
                    line_ended = true;
                }

                if (line_ended) {
                    size_t removed = removeChar('\n', line_buf, line_len);
                    line_len -= removed;
                    line_buf[line_len] = '\0';

                    if (line_len <= 0) {
                        line_len = 0;
                        line_ended = false;
                        printPrompt();
                        continue;
                    }

                    if (strcmp(line_buf, "exit") == 0) {
                        printf("exiting mshell ;_; T_T\n");
                        exit(EXIT_SUCCESS);
                        return 0;
                    }
                    if (strcmp(line_buf, "fg") == 0) {
                        if (bg_jobs.count > 0) {
                            fg_job = bg_jobs.items[bg_jobs.count - 1];
                            printJob(&fg_job);
                            vecRemoveAt(&bg_jobs, bg_jobs.count - 1);

                            /* transfer control from shell to fg_job */
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, STDIN_FILENO, NULL);
                            LOG("tcsetpgrp %i\n", fg_job.pgid);
                            int tcsetpgrp_res = tcsetpgrp(STDIN_FILENO, fg_job.pgid);
                            if (tcsetpgrp_res != 0) {
                                perror("tcsetpgrp(STDIN_FILENO, getpgid(pid));");
                            }
                            tcsetattr(STDIN_FILENO, TCSAFLUSH, &fg_job.termios_conf);

                            if (killpg(fg_job.pgid, SIGCONT) < 0) {
                                LOG("kill(%i, SIGCONT) failed\n", fg_job.pgid);
                                perror("kill(pgid, SIGCONT)");
                            }
                        } else {
                            printf("No background jobs\n");
                            printPrompt();
                        }
                        line_len = 0;
                        line_ended = false;
                        continue;
                    }
                    if (strcmp(line_buf, "bg") == 0) {
                        if (bg_jobs.count > 0) {
                            Job *last_bg_job = &bg_jobs.items[bg_jobs.count - 1];

                            if (killpg(last_bg_job->pgid, SIGCONT) < 0) {
                                LOG("kill(%i, SIGCONT) failed\n", last_bg_job->pgid);
                                perror("kill(pgid, SIGCONT)");
                            } else {
                                for (size_t i = 0; i < last_bg_job->pipeline.count; ++i) {
                                    last_bg_job->pipeline.items[i].status = PROC_STATUS_RUNNING;
                                }
                            }

                        } else {
                            printf("No background jobs\n");
                            printPrompt();
                        }
                        line_len = 0;
                        line_ended = false;
                        continue;
                    }
                    if (strcmp(line_buf, "cd") == 0) {
                        line_len = 0;
                        line_ended = false;
                        continue;
                    }

                    StrArrayVec command_pipe_sequence = {0};

                    const char pipe_delim[] = "|";
                    const char args_delim[] = " ";
                    char *pipe_strtok_saveptr = NULL;
                    char *args_strtok_saveptr = NULL;

                    char *pipe_token = strtok_r(line_buf, pipe_delim, &pipe_strtok_saveptr);
                    while (pipe_token != NULL) {

                        StrVec args = {0};
                        char *arg_token = strtok_r(pipe_token, args_delim, &args_strtok_saveptr);
                        while (arg_token != NULL) {
                            vecAppend(&args, arg_token);
                            arg_token = strtok_r(NULL, args_delim, &args_strtok_saveptr);
                        }
                        /* 'exec()' requires null-terminated array */
                        vecAppend(&args, NULL);

                        vecAppend(&command_pipe_sequence, args);
                        pipe_token = strtok_r(NULL, pipe_delim, &pipe_strtok_saveptr);
                    }

                    if (command_pipe_sequence.count == 0) {
                        printPrompt();
                    } else if (command_pipe_sequence.count == 1) {
                        Process new_proc = execCommand(&command_pipe_sequence.items[0], 0, true, epoll_fd, 0, 0, NULL, 0);
                        assert(fg_job.pipeline.items == NULL && fg_job.pipeline.count == 0 &&
                               "trying to add new process to non-empty fg_job.pipeline\n");
                        fg_job.pgid = new_proc.pid;
                        vecAppend(&fg_job.pipeline, new_proc);
                    } else {
                        Job pipe_job = pipeCommands(&command_pipe_sequence, epoll_fd);
                        assert(fg_job.pipeline.items == NULL && fg_job.pipeline.count == 0 &&
                               "trying to add new process to non-empty fg_job.pipeline\n");
                        fg_job = pipe_job;
                        // vecAppend(&fg_job.pipeline, new_proc);
                    }

                    for (int i = 0; i < command_pipe_sequence.count; ++i) {
                        vecFree(&command_pipe_sequence.items[i]);
                    }
                    vecFree(&command_pipe_sequence);
                    line_len = 0;
                    line_ended = false;
                }
            }
        }
        LOG("fd events processing finished\n");
    }
    vecFree(&fg_job.pipeline);
    vecFree(&bg_jobs);

    return 0;
}
