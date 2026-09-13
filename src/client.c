#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "client.h"

int clientQueue(struct client *client, const char *line)
{
    size_t length = strlen(line);
    if (length + 1 > OUTPUT_CAPACITY - bufferSize(&client->output)) {
        client->reason = CLOSE_BACKPRESSURE;
        return -1;
    }
    if (bufferAppend(&client->output, line, length) != 0)
        return -1;
    return bufferAppend(&client->output, "\n", 1);
}

static int handleRequest(void *context, const char *line)
{
    struct client *client = context;
    if (strcmp(line, "PING") == 0)
        return clientQueue(client, "PONG");
    if (strcmp(line, "INFO") == 0) {
        char reply[128];
        (void)snprintf(reply, sizeof(reply), "INFO id=%" PRIu64 " session=none", client->id);
        return clientQueue(client, reply);
    }
    return clientQueue(client, "ERR command");
}

void clientDrain(struct client *client, time_t now)
{
    if (client->state == CLIENT_ACTIVE) {
        client->state = CLIENT_DRAINING;
        client->drainUntil = now + DRAIN_SECONDS;
    }
}

int clientRead(struct client *client, time_t now)
{
    char data[MAX_MESSAGE_SIZE];
    size_t remaining = IO_BUDGET;
    while (remaining > 0) {
        size_t size = remaining < sizeof(data) ? remaining : sizeof(data);
        ssize_t count = recv(client->fd, data, size, 0);
        if (count > 0) {
            size_t length = (size_t)count;
            client->bytesReceived += (uint64_t)length;
            client->lastActivity = now;
            remaining -= length;
            if (frameFeed(&client->input, data, length, handleRequest, client) != 0) {
                if (client->reason != CLOSE_BACKPRESSURE)
                    client->reason = CLOSE_PROTOCOL;
                return -1;
            }
            /* Flush between reads to avoid penalizing ordinary request bursts. */
            if (clientWrite(client, now) != 0)
                return -1;
            continue;
        }
        if (count == 0) {
            if (client->input.length != 0) {
                client->reason = CLOSE_INCOMPLETE;
                return -1;
            }
            clientDrain(client, now);
            return 0;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        client->reason = errno == ECONNRESET ? CLOSE_RESET : CLOSE_READ;
        return -1;
    }
    return 0;
}

int clientWrite(struct client *client, time_t now)
{
    size_t remaining = IO_BUDGET;
    while (bufferSize(&client->output) > 0 && remaining > 0) {
        size_t length = bufferSize(&client->output);
        if (length > remaining)
            length = remaining;
        ssize_t count = send(client->fd, client->output.data + client->output.start,
                             length, MSG_NOSIGNAL);
        if (count > 0) {
            bufferConsume(&client->output, (size_t)count);
            client->bytesSent += (uint64_t)count;
            client->lastActivity = now;
            remaining -= (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 0;
        client->reason = count < 0 && (errno == ECONNRESET || errno == EPIPE)
                         ? CLOSE_RESET : CLOSE_WRITE;
        return -1;
    }
    return 0;
}

void clientClose(struct client *client)
{
    if (client->state == CLIENT_FREE || client->state == CLIENT_CLOSED)
        return;
    (void)shutdown(client->fd, SHUT_RDWR);
    /* On Linux, retrying close after EINTR could close a reused descriptor. */
    (void)close(client->fd);
    client->fd = -1;
    client->state = CLIENT_CLOSED;
}

const char *clientState(const struct client *client)
{
    return client->state == CLIENT_ACTIVE ? "active" : "draining";
}

const char *clientReason(enum close_reason reason)
{
    switch (reason) {
    case CLOSE_PEER: return "peer";
    case CLOSE_RESET: return "reset";
    case CLOSE_READ: return "read-error";
    case CLOSE_WRITE: return "write-error";
    case CLOSE_PROTOCOL: return "protocol";
    case CLOSE_INCOMPLETE: return "incomplete";
    case CLOSE_BACKPRESSURE: return "backpressure";
    case CLOSE_IDLE: return "idle";
    case CLOSE_KICK: return "kick";
    case CLOSE_SHUTDOWN: return "shutdown";
    case CLOSE_DRAIN_TIMEOUT: return "drain-timeout";
    case CLOSE_EVENT: return "event-error";
    case CLOSE_SERVER_ERROR: return "server-error";
    }
    return "unknown";
}
