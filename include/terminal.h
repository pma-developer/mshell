#pragma once
#include <termios.h>

bool terminalInit(struct termios *shell_termios);
void terminalResetOriginal(void);
void terminalIgnoreStopSignals(bool ignore);