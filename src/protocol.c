#include "protocol.h"

int frameFeed(struct frame *frame, const char *data, size_t length,
              frameHandler handler, void *context)
{
    for (size_t index = 0; index < length; ++index) {
        unsigned char byte = (unsigned char)data[index];
        if (frame->length > 0 && frame->data[frame->length - 1] == '\r' && byte != '\n')
            return -1;
        if (byte == '\n') {
            if (frame->length > 0 && frame->data[frame->length - 1] == '\r')
                --frame->length;
            frame->data[frame->length] = '\0';
            if (handler(context, frame->data) != 0)
                return -1;
            frame->length = 0;
        } else {
            if ((byte < 32 && byte != '\r' && byte != '\t') || byte > 126 ||
                frame->length == MAX_MESSAGE_SIZE)
                return -1;
            frame->data[frame->length++] = (char)byte;
        }
    }
    return 0;
}
