#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include "console.h"
#include "server_internal.h"

static void stopInput(struct console *console, int epollFd)
{
    if (console->reading)
        (void)epoll_ctl(epollFd, EPOLL_CTL_DEL, STDIN_FILENO, NULL);
    console->reading = false;
}

void consoleClose(struct console *console, int epollFd)
{
    stopInput(console, epollFd);
    if (console->writing)
        (void)epoll_ctl(epollFd, EPOLL_CTL_DEL, STDOUT_FILENO, NULL);
    console->writing = false;
    console->enabled = false;
    console->start = console->end = 0;
    if (console->inputFlags != -1)
        (void)fcntl(STDIN_FILENO, F_SETFL, console->inputFlags);
    if (console->outputFlags != -1)
        (void)fcntl(STDOUT_FILENO, F_SETFL, console->outputFlags);
    console->inputFlags = console->outputFlags = -1;
}

void consoleInit(struct console *console, int epollFd)
{
    console->inputFlags = fcntl(STDIN_FILENO, F_GETFL);
    console->outputFlags = fcntl(STDOUT_FILENO, F_GETFL);
    if (console->inputFlags == -1 || console->outputFlags == -1)
        goto fail;
    if (fcntl(STDIN_FILENO, F_SETFL, console->inputFlags | O_NONBLOCK) == -1 ||
        fcntl(STDOUT_FILENO, F_SETFL, console->outputFlags | O_NONBLOCK) == -1)
        goto fail;
    /* Require epoll-capable output: regular files can block despite O_NONBLOCK. */
    struct epoll_event event = { .events = EPOLLOUT, .data.u64 = EVENT_OUTPUT };
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, STDOUT_FILENO, &event) == -1)
        goto fail;
    console->writing = true;
    event.events = EPOLLIN;
    event.data.u64 = EVENT_INPUT;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, STDIN_FILENO, &event) == -1)
        goto fail;
    console->reading = true;
    console->enabled = true;
    return;
fail:
    consoleClose(console, epollFd);
}

void consoleWrite(struct console *console, int epollFd)
{
    while (console->start < console->end) {
        ssize_t count = write(STDOUT_FILENO, console->output + console->start,
                              console->end - console->start);
        if (count > 0) {
            console->start += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;
        consoleClose(console, epollFd);
        return;
    }
    console->start = console->end = 0;
    if (console->writing) {
        (void)epoll_ctl(epollFd, EPOLL_CTL_DEL, STDOUT_FILENO, NULL);
        console->writing = false;
    }
}

void consolePrint(struct console *console, int epollFd, const char *format, ...)
{
    if (!console->enabled)
        return;
    char line[MAX_MESSAGE_SIZE + 256];
    va_list args;
    va_start(args, format);
    int count = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= sizeof(line)) {
        consoleClose(console, epollFd);
        return;
    }
    size_t length = (size_t)count;
    size_t used = console->end - console->start;
    if (length > sizeof(console->output) - used) {
        /* A stalled operator must not stall the network loop. */
        consoleClose(console, epollFd);
        return;
    }
    if (length > sizeof(console->output) - console->end) {
        memmove(console->output, console->output + console->start, used);
        console->start = 0;
        console->end = used;
    }
    memcpy(console->output + console->end, line, length);
    console->end += length;
    if (!console->writing) {
        struct epoll_event event = { .events = EPOLLOUT, .data.u64 = EVENT_OUTPUT };
        if (epoll_ctl(epollFd, EPOLL_CTL_ADD, STDOUT_FILENO, &event) == -1) {
            consoleClose(console, epollFd);
            return;
        }
        console->writing = true;
    }
    consoleWrite(console, epollFd);
}

void consoleRead(struct console *console, int epollFd, frameHandler handler, void *context)
{
    char data[MAX_MESSAGE_SIZE];
    /* One read per event bounds command work, including repeated broadcasts. */
    ssize_t count;
    do {
        count = read(STDIN_FILENO, data, sizeof(data));
    } while (count < 0 && errno == EINTR);
    if (count > 0) {
        if (frameFeed(&console->input, data, (size_t)count, handler, context) != 0) {
            consolePrint(console, epollFd, "error console input\n");
            stopInput(console, epollFd);
        }
    } else if (count == 0) {
        if (console->input.length != 0)
            consolePrint(console, epollFd, "error incomplete command\n");
        stopInput(console, epollFd);
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        stopInput(console, epollFd);
    }
}
