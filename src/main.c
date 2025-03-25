#include "utilsc.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StrDA;

struct GlobalState {
    struct termios prev_termios;
};

struct GlobalState glb;
const int MAX_STDIN_READ_ERRORS = 5;

void resetTermios() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &glb.prev_termios); }

void execAsChild(const StrDA *args) {
    int pid = fork();
    if (pid == -1) {
        printf("fork() failed\n");
    } else if (pid == 0) {
        pid = getpid();
        int set_pgid_res = setpgid(pid, pid);
        if (set_pgid_res != 0) {
            printf("failed to setpgid\n");
        }
        if (tcsetpgrp(STDIN_FILENO, getpgid(pid)) != 0) {
            printf("failed: tcsetpgrp(STDIN_FILENO, getpgid(pid));\n");
        }
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);
        int execvn_res = execvp(args->items[0], args->items);
        if (execvn_res == -1) {
            printf("error executing: \"%s\"\n", args->items[0]);
        }
        exit(0);
    } else {
        int wstatus = 0;
        waitpid(pid, &wstatus, WUNTRACED);
        if (WIFEXITED(wstatus)) {
            int exit_status = WEXITSTATUS(wstatus);
            printf("%s(%i) exited with status %i\n", args->items[0], pid, exit_status);
        }
        if (WIFSIGNALED(wstatus)) {
            int sig_num = WTERMSIG(wstatus);
            printf("%s(%i) terminated by signal %i\n", args->items[0], pid, sig_num);

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
            printf("%s(%i) stopped by signal %i\n", args->items[0], pid, sig_num);
        }
        if (WIFCONTINUED(wstatus)) {
            printf("%s(%i) continued\n", args->items[0], pid);
        }
    }
}

void signal_handler(int signum) { printf("Received signal: %d\n", signum); }

int main(int argc, char *argv[]) {
    const size_t max_line_len = 1024;
    struct termios raw_termios;

    tcgetattr(STDIN_FILENO, &glb.prev_termios);
    atexit(resetTermios);

    raw_termios = glb.prev_termios;
    raw_termios.c_lflag &= ~(ICANON | ISIG | TOSTOP);

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_termios);

    char line_buf[max_line_len + 1];
    size_t line_len = 0;
    ssize_t read_count = 0;

    bool line_ended = false;

    printf("mshell v0.0.0 *_*\n\n");
    while (true) {
        size_t read_error_count = 0;
        while (!line_ended && line_len < max_line_len && (read_count = read(STDIN_FILENO, line_buf + line_len, 1))) {

            if (read_count < 0) {
                printf("errno: %d\n", errno);
                perror("Error reading STDIN");
                read_error_count++;
                if (read_error_count >= MAX_STDIN_READ_ERRORS) {
                    printf("Too many STDIN read errors\n", errno);
                    exit(1);
                }
                continue;
            }
            read_error_count = 0;
            line_len += read_count;
            // printf("read %zi chars; line_len=%zu\n", read_count, line_len);
            char last_char = line_buf[line_len - 1];
            // printf("last char is %c(%hhi)\n", last_char, last_char);

            /* Handle character */
            if ((unsigned char)last_char == raw_termios.c_cc[VINTR]) {
                printf("received VINTR\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VQUIT]) {
                printf("received VQUIT\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VERASE]) {
                printf("received VERASE\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VKILL]) {
                printf("received VKILL\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VEOF]) {
                printf("received VEOF\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VTIME]) {
                printf("received VTIME\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VMIN]) {
                printf("received VMIN\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VSWTC]) {
                printf("received VSWTC\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VSTART]) {
                printf("received VSTART\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VSTOP]) {
                printf("received VSTOP\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VSUSP]) {
                printf("received VSUSP\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VEOL]) {
                printf("received VEOL\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VREPRINT]) {
                printf("received VREPRINT\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VDISCARD]) {
                printf("received VDISCARD\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VWERASE]) {
                printf("received VWERASE\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VLNEXT]) {
                printf("received VLNEXT\n");
            } else if ((unsigned char)last_char == raw_termios.c_cc[VEOL2]) {
                printf("received VEOL2\n");
            } else if (last_char == '\n') {
                printf("hit new line!\n");
                line_ended = true;
            }
        }
        if (line_len >= max_line_len) {
            printf("exceeded max_line_len(%zu)\n", max_line_len);
            line_ended = true;
        }

        if (line_ended) {
            line_buf[line_len] = '\0';

            printf("Writing input line to stdout: \n");
            write(STDOUT_FILENO, line_buf, line_len);

            size_t removed = removeChar('\n', line_buf, line_len);
            line_len -= removed;
            line_buf[line_len] = '\0';

            if (line_len > 0 && strncmp(line_buf, "exit", line_len) == 0) {
                printf("exiting mshell ;_; T_T\n");
                exit(0);
                return 0;
            }

            StrDA args = {0};
            char *token = strtok(line_buf, " ");
            while (token != NULL) {
                daAppend(&args, token);
                token = strtok(NULL, " ");
            }
            daAppend(&args, NULL);

            execAsChild(&args);
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_termios);
            tcsetpgrp(STDIN_FILENO, getpgid(0));
            daFree(&args);
            line_len = 0;
            line_ended = false;
        }
    }

    return 0;
}
