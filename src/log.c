#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "log.h"

static void writeLog(const char *line)
{
    /* Diagnostics are best effort; a full nonblocking pipe drops the line. */
    ssize_t count;
    do {
        count = write(STDERR_FILENO, line, strlen(line));
    } while (count < 0 && errno == EINTR);
}

void logError(const char *operation)
{
    int error = errno;
    char line[512];
    (void)snprintf(line, sizeof(line), "error %s: %s\n", operation, strerror(error));
    writeLog(line);
}

void logEvent(bool verbose, const char *format, ...)
{
    if (!verbose)
        return;
    char line[512];
    va_list args;
    va_start(args, format);
    int count = vsnprintf(line, sizeof(line) - 1, format, args);
    va_end(args);
    if (count < 0)
        return;
    size_t length = strlen(line);
    line[length] = '\n';
    line[length + 1] = '\0';
    writeLog(line);
}
