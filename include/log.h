#ifndef NETD_LOG_H
#define NETD_LOG_H
#include <stdbool.h>
void logError(const char *operation);
void logEvent(bool verbose, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
#endif
