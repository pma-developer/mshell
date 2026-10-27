//
// Created by pmapma on 10/27/26.
//

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

static struct termios saved_termios;
static bool terminal_saved = false;

bool terminalInit(struct termios *shell_termios) {
    if (tcgetattr(STDIN_FILENO, &saved_termios) != 0) {
        perror("tcgetattr");
        return false;
    }

    terminal_saved = true;
    *shell_termios = saved_termios;

    shell_termios->c_lflag &= ~(ISIG | TOSTOP);

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, shell_termios) != 0) {
        perror("tcsetattr");
        return false;
    }

    return true;
}

void terminalResetOriginal(void) {
    if (terminal_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
    }
}

void terminalIgnoreStopSignals(bool ignore) {
    __sighandler_t handler = ignore ? SIG_IGN : SIG_DFL;

    signal(SIGINT, handler);
    signal(SIGQUIT, handler);
    signal(SIGTSTP, handler);
    signal(SIGTTIN, handler);
    signal(SIGTTOU, handler);
}