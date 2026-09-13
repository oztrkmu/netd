#ifndef NETD_SERVER_INTERNAL_H
#define NETD_SERVER_INTERNAL_H
#include "client.h"
#include "console.h"
#include "server.h"

enum event_tag { EVENT_LISTEN, EVENT_SIGNAL, EVENT_INPUT, EVENT_OUTPUT, EVENT_CLIENT };
struct disconnect_record {
    uint64_t id;
    char host[64];
    char port[6];
    enum close_reason reason;
    time_t at;
    time_t duration;
    uint64_t received;
    uint64_t sent;
    size_t pending;
};
struct server {
    int epollFd;
    int listenFd;
    int signalFd;
    bool verbose;
    bool stopping;
    bool listenerPaused;
    time_t now;
    time_t startedAt;
    time_t acceptAfter;
    time_t stopAt;
    size_t clientCount;
    size_t peakClients;
    uint64_t disconnected;
    size_t historyCount;
    size_t historyNext;
    struct disconnect_record history[HISTORY_CAPACITY];
    size_t nextSlot;
    uint64_t nextId;
    uint64_t accepted;
    uint64_t rejected;
    uint64_t bytesReceived;
    uint64_t bytesSent;
    struct console console;
    struct client clients[MAX_CLIENTS];
};
void serverDrop(struct server *server, struct client *client, enum close_reason reason);
void serverWatch(struct server *server, struct client *client);
void serverStop(struct server *server);
int operatorCommand(void *context, const char *line);
#endif
