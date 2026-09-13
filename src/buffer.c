#include <string.h>
#include "buffer.h"

size_t bufferSize(const struct buffer *buffer)
{
    return buffer->end - buffer->start;
}

int bufferAppend(struct buffer *buffer, const char *data, size_t length)
{
    size_t used = bufferSize(buffer);
    if (length > sizeof(buffer->data) - used)
        return -1;
    if (length > sizeof(buffer->data) - buffer->end) {
        memmove(buffer->data, buffer->data + buffer->start, used);
        buffer->start = 0;
        buffer->end = used;
    }
    memcpy(buffer->data + buffer->end, data, length);
    buffer->end += length;
    return 0;
}

void bufferConsume(struct buffer *buffer, size_t length)
{
    buffer->start += length;
    if (buffer->start == buffer->end)
        buffer->start = buffer->end = 0;
}
