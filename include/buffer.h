#ifndef NETD_BUFFER_H
#define NETD_BUFFER_H
#include <stddef.h>
#include "config.h"

struct buffer {
    char data[OUTPUT_CAPACITY];
    size_t start;
    size_t end;
};
size_t bufferSize(const struct buffer *buffer);
int bufferAppend(struct buffer *buffer, const char *data, size_t length);
void bufferConsume(struct buffer *buffer, size_t length);
#endif
