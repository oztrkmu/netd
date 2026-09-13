#ifndef NETD_PROTOCOL_H
#define NETD_PROTOCOL_H
#include <stddef.h>
#include "config.h"

struct frame {
    char data[MAX_MESSAGE_SIZE + 1];
    size_t length;
};
/* A callback may queue a reply; nonzero terminates processing immediately. */
typedef int (*frameHandler)(void *context, const char *line);
int frameFeed(struct frame *frame, const char *data, size_t length,
              frameHandler handler, void *context);
#endif
