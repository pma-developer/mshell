#define ENABLE_LOGS 0
#include "utilsc.h"
#include <bits/types/sigset_t.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

typedef enum PipeFdType {
    PIPE_READ = 0,
    PIPE_WRITE = 1,
} PipeFdType;

typedef enum ProcStatus {
    PROC_STATUS_UNDEFINED,
    PROC_STATUS_TERMINATED,
    PROC_STATUS_SUSPENDED,
    PROC_STATUS_RUNNING,
    PROC_STATUS_NOT_CHANGED,
} ProcStatus;

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StrDA;

typedef struct {
    pid_t pid;
    const char *name;
    ProcStatus status;
} Process;

typedef struct {
    StrDA *items;
    size_t count;
    size_t capacity;
} StrArrayDA;

struct GlobalState {
    struct termios prev_termios;
};

typedef struct {
    Process *items;
    size_t count;
    size_t capacity;
} ProcessDA;

typedef struct {
    ProcessDA pipeline;
    pid_t pgid;
    struct termios termios_conf;
} Job;

typedef struct {
    Job *items;
    size_t count;
    size_t capacity;
} JobsDA;

struct GlobalState glb;
const int MAX_STDIN_READ_ERRORS = 5;
int epoll_fd;

#define MAX_EPOLL_EVENTS 10
#define MAX_PROMPT_SUB_STR_SIZE 128
#define PROMPT_SIZE ((MAX_PROMPT_SUB_STR_SIZE * 3) + 50)

void resetTermios() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &glb.prev_termios); }

void freeProcessFields(const Process *process) {
    log("freeing process->name %p\n", process->name);
    free((void *)process->name);
}

void printProcess(const Process *proc) {
    log("Process PID: %d\n", proc->pid);
    log("Process Name: %s\n", proc->name);
    log("Process Status: %d\n", (int)proc->status);
}
void printJob(const Job *job) {
    if (job == NULL) {
        log("Job is NULL.\n");
        return;
    }
    log("Job PGID: %d\n", job->pgid);
    log("========================\n");

    size_t index = 0;
    while (index < job->pipeline.count) {
        printProcess(&job->pipeline.items[index]);
        log("------------------------\n");
        index++;
    }
}

void printWStatusDesc(int wstatus, const char *procname, pid_t pid) {
    if (WIFEXITED(wstatus)) {
        int exit_status = WEXITSTATUS(wstatus);
        if (exit_status != 0) {
            printf("\n%s(%i) exited with status %i\n", procname, pid, exit_status);
        }
    }
    if (WIFSIGNALED(wstatus)) {
        int sig_num = WTERMSIG(wstatus);
        printf("\n%s(%i) terminated by signal %i\n", procname, pid, sig_num);

        bool produced_core_dump = false;
#ifdef WCOREDUMP
        produced_core_dump = WCOREDUMP(wstatus);
        if (produced_core_dump) {
            printf("Produced core dump.\n");
        }
#endif
    }
    if (WIFSTOPPED(wstatus)) {
        int sig_num = WSTOPSIG(wstatus);
        printf("\n%s(%i) stopped by signal %i\n", procname, pid, sig_num);
    }
    if (WIFCONTINUED(wstatus)) {
        printf("\n%s(%i) continued\n", procname, pid);
    }
}

void ignoreStopSignals(bool ignore) {
    __sighandler_t handler = ignore ? SIG_IGN : SIG_DFL;

    signal(SIGINT, handler);
    signal(SIGQUIT, handler);
    signal(SIGTSTP, handler);
    signal(SIGTTIN, handler);
    signal(SIGTTOU, handler);
}

ProcStatus waitProcStatus(pid_t pid) {
    int wstatus = 0;
    pid_t wait_res = waitpid(pid, &wstatus, WNOHANG | WUNTRACED | WCONTINUED);
    if (wait_res == -1) {
        log("errno=%i\n", errno);
        perror("waitpid(WNOHANG | WUNTRACED)");
        return PROC_STATUS_UNDEFINED;
    }
    if (wait_res == 0) {
        return PROC_STATUS_NOT_CHANGED;
    }
    if (wait_res > 0) {
        bool terminated = WIFEXITED(wstatus) || WIFSIGNALED(wstatus);
        bool suspended = WIFSTOPPED(wstatus);
        printWStatusDesc(wstatus, "proc", pid);

        if (!(terminated ^ suspended)) {
            return PROC_STATUS_UNDEFINED;
        }
        return terminated ? PROC_STATUS_TERMINATED : PROC_STATUS_SUSPENDED;
    }
    return PROC_STATUS_UNDEFINED;
}

char *cloneStr(const char *src) {
    char *copy = malloc(sizeof(char) * (strlen(src) + 1));
    strcpy(copy, src);
    return copy;
}

/* TODO: make PipeParams struct; replace last 4 arguments with 'const PipeParams *pipe_params' */
Process execCommand(const StrDA *command, pid_t pgid, bool set_pg_to_fg, int stdin_fd, int stdout_fd, int *fds_to_close,
                    size_t fds_count) {
    if (set_pg_to_fg) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, STDIN_FILENO, NULL);
    }

    int pid = fork();
    if (pid == -1) {
        perror("fork()");
    } else if (pid == 0) {

        int setpgid_res = setpgid(pid, pgid);
        int tcsetpgrp_res = 0;
        if (set_pg_to_fg) {
            tcsetpgrp_res = tcsetpgrp(STDIN_FILENO, getpgid(pid));
        }

        /* Reseting termios */
        resetTermios();
        ignoreStopSignals(false);

        if (setpgid_res != 0) {
            perror("setpgid");
        }
        if (tcsetpgrp_res != 0) {
            perror("tcsetpgrp(STDIN_FILENO, getpgid(pid));");
        }

        log("configuring pipe\n");
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
    log("allocated process.name %s(%p)", process.name, process.name);
    process.status = PROC_STATUS_RUNNING;
    return process;
}

Job pipeCommands(const StrArrayDA *commandsDa) {
    size_t pipefds_count = 2 * (commandsDa->count - 1);
    int pipefds[pipefds_count];
    for (size_t i = 0; i < pipefds_count; i += 2) {
        if (pipe(pipefds + i)) {
            perror("pipe(pipefds)");
            return (Job){0};
        }
    }
    Job pipe_job = {0};
    Process first_proc = execCommand(&commandsDa->items[0], 0, true, 0, pipefds[PIPE_WRITE], pipefds, pipefds_count);
    daAppend(&pipe_job.pipeline, first_proc);
    pipe_job.pgid = first_proc.pid;

    for (size_t i = 1; i < commandsDa->count - 1; ++i) {
        size_t fd_i = 2 * (i - 1);
        Process proc = execCommand(&commandsDa->items[i], first_proc.pid, false, pipefds[fd_i], pipefds[fd_i + 3],
                                   pipefds, pipefds_count);
        daAppend(&pipe_job.pipeline, proc);
    }

    Process last_proc = execCommand(&commandsDa->items[commandsDa->count - 1], first_proc.pid, false,
                                    pipefds[pipefds_count - 2], 0, pipefds, pipefds_count);
    daAppend(&pipe_job.pipeline, last_proc);

    for (size_t i = 0; i < pipefds_count; ++i) {
        close(pipefds[i]);
    }

    return pipe_job;
}

bool anyProcessStatus(const ProcessDA *process_da, ProcStatus status) {
    for (size_t i = 0; i < process_da->count; ++i) {
        if (process_da->items[i].status == status) {
            return true;
        }
    }
    return false;
}

bool updateProcStatuses(const ProcessDA *process_da) {
    bool any_updated = false;
    for (size_t i = 0; i < process_da->count; ++i) {
        Process *proc = &process_da->items[i];
        if (proc->status == PROC_STATUS_TERMINATED) {
            continue;
        }

        ProcStatus proc_status = waitProcStatus(proc->pid);
        if (proc_status == PROC_STATUS_UNDEFINED) {
            log("%s(%i) returned PROC_STATUS_UNDEFINED\n", proc->name, proc->pid);
        }
        if (proc_status != PROC_STATUS_NOT_CHANGED) {
            proc->status = proc_status;
            any_updated = true;
        }
    }
    return any_updated;
}

ProcStatus getPipelineStatus(const ProcessDA *process_da) {
    bool anyUndefined = false, anyRunning = false, anySuspended = false, anyTerminated = false;

    for (size_t i = 0; i < process_da->count; ++i) {
        const Process *proc = &process_da->items[i];
        switch (proc->status) {
        case PROC_STATUS_UNDEFINED:
            anyUndefined = true;
            break;

        case PROC_STATUS_SUSPENDED:
            anySuspended = true;
            break;

        case PROC_STATUS_RUNNING:
            anyRunning = true;
            break;

        case PROC_STATUS_TERMINATED:
            anyTerminated = true;
            break;
        case PROC_STATUS_NOT_CHANGED:
            log("process_da contains process with invalid status (%s(%i))", proc->name, proc->pid);
            return PROC_STATUS_UNDEFINED;
        }
    }

    if (anyUndefined) {
        return PROC_STATUS_UNDEFINED;
    }
    if (anyRunning) {
        return PROC_STATUS_RUNNING;
    }
    if (anySuspended) {
        return PROC_STATUS_SUSPENDED;
    }
    if (anyTerminated) {
        return PROC_STATUS_TERMINATED;
    }

    log("unable to determine pipeline status\n");
    return PROC_STATUS_UNDEFINED;
}

Job copyJob(Job job) {
    Job copy = job;
    copy.pipeline = (ProcessDA){0};
    for (size_t i = 0; i < job.pipeline.count; ++i) {
        Process job_proc = job.pipeline.items[i];
        Process copy_proc = job_proc;
        copy_proc.name = cloneStr(job_proc.name);
        daAppend(&copy.pipeline, copy_proc);
    }

    return copy;
}

bool indexOfPid(const ProcessDA *process_da, pid_t pid, size_t *index) {
    for (size_t i = 0; i < process_da->count; ++i) {
        if (process_da->items[i].pid == pid) {
            *index = i;
            return true;
        }
    }
    return false;
}

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

    tcgetattr(STDIN_FILENO, &glb.prev_termios);
    atexit(resetTermios);

    shell_termios = glb.prev_termios;
    shell_termios.c_lflag &= ~(ISIG | TOSTOP);

    ignoreStopSignals(true);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &shell_termios);

    char line_buf[max_line_len + 1];
    size_t line_len = 0;

    bool line_ended = false;

    JobsDA bg_jobs = {0};
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
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
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

        log("starting epoll wait\n");
        const int ready_fds = epoll_wait(epoll_fd, event_buf, MAX_EPOLL_EVENTS, -1);
        if (ready_fds == -1) {
            perror("epoll wait");
        }
        log("epoll ready_fds: %i\n", ready_fds);

        for (size_t fd_i = 0; fd_i < ready_fds; ++fd_i) {
            if (event_buf[fd_i].data.fd == sig_chld_fd) {
                struct signalfd_siginfo sig_chld_info;
                ssize_t read_count = 0;
                log("SIGCHLD is available for read;\n");

                while ((read_count = read(sig_chld_fd, &sig_chld_info, sizeof(sig_chld_info)))) {
                    log("reading sig_chld_fd\n");
                    if (read_count < 0) {
                        if (errno == EAGAIN) {
                            break;
                        }
                        perror("read(sig_chld_fd)");
                        continue;
                    }

                    updateProcStatuses(&fg_job.pipeline);
                    for (size_t i = 0; i < bg_jobs.count; ++i) {
                        Job *bg_job = &bg_jobs.items[i];
                        bool any_updated = updateProcStatuses(&bg_job->pipeline);

                        ProcStatus bg_job_status = getPipelineStatus(&bg_job->pipeline);
                        if ((bg_job_status == PROC_STATUS_SUSPENDED && any_updated)
                            || bg_job_status == PROC_STATUS_TERMINATED) {

                            if (bg_job_status == PROC_STATUS_SUSPENDED) {
                                log("termios of bg_job(%i) set\n", bg_job->pgid);
                                tcgetattr(STDIN_FILENO, &bg_job->termios_conf);
                            } else if (bg_job_status == PROC_STATUS_TERMINATED) {
                                daRemoveAt(&bg_jobs, i);
                                for (size_t i = 0; i < bg_job->pipeline.count; ++i) {
                                    freeProcessFields(&bg_job->pipeline.items[i]);
                                }
                                daFree(&bg_job->pipeline);
                            }
                            printPrompt();
                        }
                    }

                    fg_status = getPipelineStatus(&fg_job.pipeline);

                    log("fg_status %i\n", fg_status);
                    if (fg_status == PROC_STATUS_SUSPENDED || fg_status == PROC_STATUS_TERMINATED) {
                        if (fg_status == PROC_STATUS_SUSPENDED) {
                            tcgetattr(STDIN_FILENO, &fg_job.termios_conf);
                            daAppend(&bg_jobs, fg_job);
                        } else if (fg_status == PROC_STATUS_TERMINATED) {
                            for (size_t i = 0; i < fg_job.pipeline.count; ++i) {
                                freeProcessFields(&fg_job.pipeline.items[i]);
                            }
                            daFree(&fg_job.pipeline);
                        }
                        fg_job = (Job){0};
                        /* return control to the shell */
                        log("returning controll to the shell\n");
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
                log("sigchld processed\n");
            } else if (event_buf[fd_i].data.fd == STDIN_FILENO) {
                size_t read_error_count = 0;
                ssize_t read_count = 0;

                log("STDIN is available for read;\n");

                while (!line_ended && line_len < max_line_len &&
                       (read_count = read(STDIN_FILENO, line_buf + line_len, 1))) {

                    if (read_count < 0) {
                        log("errno: %d\n", errno);
                        perror("Error reading STDIN");
                        read_error_count++;
                        if (read_error_count >= MAX_STDIN_READ_ERRORS) {
                            log("Too many STDIN read errors\n");
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

                    if (strncmp(line_buf, "exit", line_len) == 0) {
                        printf("exiting mshell ;_; T_T\n");
                        exit(EXIT_SUCCESS);
                        return 0;
                    }
                    if (strncmp(line_buf, "fg", line_len) == 0) {
                        if (bg_jobs.count > 0) {
                            fg_job = bg_jobs.items[bg_jobs.count - 1];
                            printJob(&fg_job);
                            daRemoveAt(&bg_jobs, bg_jobs.count - 1);

                            /* transfer control from shell to fg_job */
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, STDIN_FILENO, NULL);
                            log("tcsetpgrp %i\n", fg_job.pgid);
                            int tcsetpgrp_res = tcsetpgrp(STDIN_FILENO, fg_job.pgid);
                            if (tcsetpgrp_res != 0) {
                                perror("tcsetpgrp(STDIN_FILENO, getpgid(pid));");
                            }
                            tcsetattr(STDIN_FILENO, TCSAFLUSH, &fg_job.termios_conf);

                            if (killpg(fg_job.pgid, SIGCONT) < 0) {
                                log("kill(%i, SIGCONT) failed\n", fg_job.pgid);
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
                    if (strncmp(line_buf, "bg", line_len) == 0) {
                        if (bg_jobs.count > 0) {
                            Job *last_bg_job = &bg_jobs.items[bg_jobs.count - 1];

                            if (killpg(last_bg_job->pgid, SIGCONT) < 0) {
                                log("kill(%i, SIGCONT) failed\n", last_bg_job->pgid);
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
                    if (strncmp(line_buf, "cd", line_len) == 0) {
                        line_len = 0;
                        line_ended = false;
                        continue;
                    }

                    StrArrayDA command_pipe_sequence = {0};

                    const char pipe_delim[] = "|";
                    const char args_delim[] = " ";
                    char *pipe_strtok_saveptr = NULL;
                    char *args_strtok_saveptr = NULL;

                    char *pipe_token = strtok_r(line_buf, pipe_delim, &pipe_strtok_saveptr);
                    while (pipe_token != NULL) {

                        StrDA args = {0};
                        char *arg_token = strtok_r(pipe_token, args_delim, &args_strtok_saveptr);
                        while (arg_token != NULL) {
                            daAppend(&args, arg_token);
                            arg_token = strtok_r(NULL, args_delim, &args_strtok_saveptr);
                        }
                        /* 'exec()' requires null-terminated array */
                        daAppend(&args, NULL);

                        daAppend(&command_pipe_sequence, args);
                        pipe_token = strtok_r(NULL, pipe_delim, &pipe_strtok_saveptr);
                    }

                    if (command_pipe_sequence.count > 0) {
                        if (command_pipe_sequence.count == 1) {
                            Process new_proc = execCommand(&command_pipe_sequence.items[0], 0, true, 0, 0, NULL, 0);
                            assert(fg_job.pipeline.items == NULL && fg_job.pipeline.count == 0 &&
                                   "trying to add new process to non-empty fg_job.pipeline\n");
                            fg_job.pgid = new_proc.pid;
                            daAppend(&fg_job.pipeline, new_proc);
                        } else {
                            Job pipe_job = pipeCommands(&command_pipe_sequence);
                            assert(fg_job.pipeline.items == NULL && fg_job.pipeline.count == 0 &&
                                   "trying to add new process to non-empty fg_job.pipeline\n");
                            fg_job = pipe_job;
                            // daAppend(&fg_job.pipeline, new_proc);
                        }
                    } else {
                        printPrompt();
                    }

                    for (int i = 0; i < command_pipe_sequence.count; ++i) {
                        daFree(&command_pipe_sequence.items[i]);
                    }
                    daFree(&command_pipe_sequence);
                    line_len = 0;
                    line_ended = false;
                }
            }
        }
        log("fd events processing finished\n");
    }
    daFree(&fg_job.pipeline);
    daFree(&bg_jobs);

    return 0;
}
