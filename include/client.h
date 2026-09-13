#ifndef NETD_CLIENT_H
#define NETD_CLIENT_H
#include <stdint.h>
#include <time.h>
#include "buffer.h"
#include "protocol.h"

enum client_state { CLIENT_FREE, CLIENT_ACTIVE, CLIENT_DRAINING, CLIENT_CLOSED };
enum close_reason { CLOSE_PEER, CLOSE_RESET, CLOSE_READ, CLOSE_WRITE,
                    CLOSE_PROTOCOL, CLOSE_INCOMPLETE, CLOSE_BACKPRESSURE,
                    CLOSE_IDLE, CLOSE_KICK, CLOSE_SHUTDOWN, CLOSE_DRAIN_TIMEOUT,
                    CLOSE_EVENT, CLOSE_SERVER_ERROR };
struct client {
    uint64_t id;
    /* Zero means unassigned. A future session store owns logical state. */
    uint64_t sessionId;
    int fd;
    char host[64];
    char port[6];
    /* Wall time for display; activity and deadlines use monotonic seconds. */
    time_t connectedAt;
    time_t connectedMono;
    time_t lastActivity;
    time_t drainUntil;
    enum client_state state;
    enum close_reason reason;
    uint64_t bytesReceived;
    uint64_t bytesSent;
    struct frame input;
    struct buffer output;
};
int clientQueue(struct client *client, const char *line);
int clientRead(struct client *client, time_t now);
int clientWrite(struct client *client, time_t now);
void clientDrain(struct client *client, time_t now);
void clientClose(struct client *client);
const char *clientReason(enum close_reason reason);
const char *clientState(const struct client *client);
#endif
