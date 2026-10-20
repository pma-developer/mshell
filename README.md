# mshell - A Unix Shell Implementation

A learning project implementing a Unix shell with job control, pipelines, and signal handling.

## Features

- **Job Control**: Foreground and background job management with `fg` and `bg` commands
- **Pipelines**: Support for command piping (e.g., `ls | grep foo`)
- **Signal Handling**: Proper handling of SIGINT, SIGTSTP, SIGCHLD, and other signals
- **Process Groups**: Correct process group management for terminal control
- **Async I/O**: Uses epoll + signalfd for efficient event-driven architecture

## Building

```bash
make
```

## Running

```bash
make run
```

Or directly:

```bash
./build/main
```

## Architecture

The shell uses an event-driven architecture with:
- **epoll**: For monitoring stdin and signal file descriptors
- **signalfd**: For receiving SIGCHLD notifications asynchronously
- **termios**: For terminal mode management
- **Custom Dynamic Arrays**: Generic DA macros for resizable containers

## Built-in Commands

- `exit` - Exit the shell
- `fg` - Bring most recent background job to foreground
- `bg` - Continue most recent background job in background

## Development

The project uses modern C tooling:
- **clang-format**: Code formatting (LLVM style, 120 char limit)
- **clang-tidy**: Static analysis with security and readability checks
- **AddressSanitizer**: Memory error detection
- **LeakSanitizer**: Memory leak detection

## Technical Details

- **Language**: C23 (GNU23 standard)
- **Compiler**: GCC with sanitizers enabled
- **Platform**: Linux (uses Linux-specific syscalls: epoll, signalfd)

## Limitations

- Fixed input buffer size (1024 bytes)
- No command history
- No tab completion
- No environment variable expansion
- `cd` command is not implemented

## Future Improvements

- Modularize code into separate files (job.c, parser.c, executor.c, terminal.c)
- Remove global state
- Add command history
- Add tab completion
- Add environment variable support
