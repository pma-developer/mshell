#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>

struct globalState {
    struct termios prev_termios;
};

struct globalState glb;

void resetTermios() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &glb.prev_termios);
}

int main(int argc, char *argv[]) {
    const size_t max_line_len = 1024;

    struct termios raw_termios;
    struct termios test_termios;

    tcgetattr(STDIN_FILENO, &glb.prev_termios);
    atexit(resetTermios);

    raw_termios = glb.prev_termios;
    raw_termios.c_lflag &= ~(ECHO);

    tcgetattr(STDIN_FILENO, &test_termios);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_termios);
    tcgetattr(STDIN_FILENO, &test_termios);

    
    char line_buf[max_line_len + 1];
    size_t line_len = 0;
    ssize_t read_count = 0;

    bool line_ended = false;

    printf("mshell v 0.0.0 *_*\n\n");
    while (true) {
        while (!line_ended && line_len < max_line_len && (read_count = read(STDIN_FILENO, line_buf + line_len, 1))) {

            line_len += read_count;
            printf("read %zi chars; last char is %c\n", read_count, line_buf[line_len - 1]);
            if(line_buf[line_len - 1] == '\n') {
                printf("hit new line!\n");
                line_ended = true;
            }
        }
        if (line_len >= max_line_len) {
            printf("exceeded max_line_len(%zu)\n", max_line_len);
            line_ended = true;
        }

        if (line_ended){
            line_buf[line_len] = '\0';

            printf("Writing input line to stdout: \n");
            write(STDOUT_FILENO, line_buf, line_len);

            if(strncmp(line_buf, "exit\n", line_len) == 0) {
                printf("exiting mshell ;_; T_T\n");
                exit(0);
                return 0;
            }
            line_len = 0;
            line_ended = false;
        }
    }

    return 0;
}
