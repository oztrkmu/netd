#ifndef NETD_CONSOLE_H
#define NETD_CONSOLE_H
#include <stdbool.h>
#include <stddef.h>
#include "protocol.h"

struct console {
    struct frame input;
    char output[CONSOLE_CAPACITY];
    size_t start;
    size_t end;
    int inputFlags;
    int outputFlags;
    bool reading;
    bool writing;
    bool enabled;
};
void consoleInit(struct console *console, int epollFd);
void consoleClose(struct console *console, int epollFd);
void consoleRead(struct console *console, int epollFd, frameHandler handler, void *context);
void consoleWrite(struct console *console, int epollFd);
void consolePrint(struct console *console, int epollFd, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
#endif
