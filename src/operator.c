#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "command.h"
#include "server_internal.h"

static struct client *findClient(struct server *server, uint64_t id)
{
    for (size_t index = 0; index < MAX_CLIENTS; ++index) {
        struct client *client = &server->clients[index];
        if (client->id == id && (client->state == CLIENT_ACTIVE || client->state == CLIENT_DRAINING))
            return client;
    }
    return NULL;
}

static void showClient(struct server *server, const struct client *client, bool detail)
{
    consolePrint(&server->console, server->epollFd,
                 "id=%" PRIu64 " fd=%d addr=[%s]:%s state=%s\n",
                 client->id, client->fd, client->host, client->port, clientState(client));
    if (detail)
        consolePrint(&server->console, server->epollFd,
                     "session=none connected=%jd age=%jd idle=%jd rx=%" PRIu64 " tx=%" PRIu64 " queued=%zu\n",
                     (intmax_t)client->connectedAt, (intmax_t)(server->now - client->connectedMono),
                     (intmax_t)(server->now - client->lastActivity),
                     client->bytesReceived, client->bytesSent, bufferSize(&client->output));
}

static bool sendMessage(struct server *server, struct client *client, const char *message)
{
    char line[MAX_MESSAGE_SIZE + 5];
    if (client->state != CLIENT_ACTIVE)
        return false;
    (void)snprintf(line, sizeof(line), "MSG %s", message);
    if (clientQueue(client, line) != 0) {
        serverDrop(server, client, CLOSE_BACKPRESSURE);
        return false;
    }
    serverWatch(server, client);
    return client->state == CLIENT_ACTIVE;
}

int operatorCommand(void *context, const char *line)
{
    struct server *server = context;
    if (server->stopping || !server->console.enabled)
        return 0;
    char copy[MAX_MESSAGE_SIZE + 1];
    memcpy(copy, line, strlen(line) + 1);
    struct command command = commandParse(copy);
    struct client *client = NULL;
    if (command.kind == CMD_INFO || command.kind == CMD_SEND || command.kind == CMD_KICK) {
        client = findClient(server, command.id);
        if (client == NULL) {
            consolePrint(&server->console, server->epollFd, "error client=%" PRIu64 " not-found\n", command.id);
            return 0;
        }
    }
    switch (command.kind) {
    case CMD_HELP: {
        size_t count;
        const struct command_spec *commands = commandSpecs(&count);
        for (size_t index = 0; index < count; ++index)
            consolePrint(&server->console, server->epollFd, "%-22s %s\n",
                         commands[index].usage, commands[index].description);
        break;
    }
    case CMD_LIST:
        for (size_t index = 0; index < MAX_CLIENTS; ++index) {
            const struct client *entry = &server->clients[index];
            if (entry->state == CLIENT_ACTIVE || entry->state == CLIENT_DRAINING)
                showClient(server, entry, false);
        }
        consolePrint(&server->console, server->epollFd, "clients=%zu\n", server->clientCount);
        break;
    case CMD_INFO:
        showClient(server, client, true);
        break;
    case CMD_SEND:
        if (sendMessage(server, client, command.message))
            consolePrint(&server->console, server->epollFd, "queued id=%" PRIu64 "\n", command.id);
        else
            consolePrint(&server->console, server->epollFd, "error send id=%" PRIu64 "\n", command.id);
        break;
    case CMD_BROADCAST: {
        size_t queued = 0;
        for (size_t index = 0; index < MAX_CLIENTS; ++index)
            if (sendMessage(server, &server->clients[index], command.message))
                ++queued;
        consolePrint(&server->console, server->epollFd, "queued=%zu\n", queued);
        break;
    }
    case CMD_KICK:
        serverDrop(server, client, CLOSE_KICK);
        consolePrint(&server->console, server->epollFd, "closed id=%" PRIu64 " reason=kick\n", command.id);
        break;
    case CMD_STATS: {
        uint64_t received = server->bytesReceived;
        uint64_t sent = server->bytesSent;
        for (size_t index = 0; index < MAX_CLIENTS; ++index) {
            const struct client *entry = &server->clients[index];
            if (entry->state == CLIENT_ACTIVE || entry->state == CLIENT_DRAINING) {
                received += entry->bytesReceived;
                sent += entry->bytesSent;
            }
        }
        consolePrint(&server->console, server->epollFd,
                     "clients=%zu accepted=%" PRIu64 " rejected=%" PRIu64 " rx=%" PRIu64 " tx=%" PRIu64 " closed=%" PRIu64 " peak=%zu limit=%d uptime=%jd\n",
                     server->clientCount, server->accepted, server->rejected, received, sent,
                     server->disconnected, server->peakClients, MAX_CLIENTS,
                     (intmax_t)(server->now - server->startedAt));
        break;
    }
    case CMD_HISTORY:
        for (size_t index = 0; index < server->historyCount; ++index) {
            size_t slot = (server->historyNext + HISTORY_CAPACITY - server->historyCount + index)
                          % HISTORY_CAPACITY;
            const struct disconnect_record *record = &server->history[slot];
            consolePrint(&server->console, server->epollFd,
                         "id=%" PRIu64 " addr=[%s]:%s reason=%s at=%jd age=%jd rx=%" PRIu64
                         " tx=%" PRIu64 " pending=%zu\n",
                         record->id, record->host, record->port, clientReason(record->reason),
                         (intmax_t)record->at, (intmax_t)record->duration,
                         record->received, record->sent, record->pending);
        }
        consolePrint(&server->console, server->epollFd, "entries=%zu\n", server->historyCount);
        break;
    case CMD_QUIT:
        serverStop(server);
        break;
    case CMD_INVALID:
        consolePrint(&server->console, server->epollFd, "error command\n");
        break;
    }
    return 0;
}
