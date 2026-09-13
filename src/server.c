#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include "log.h"
#include "socket.h"
#include "server_internal.h"

static time_t serverNow(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
        return (time_t)-1;
    return now.tv_sec;
}

static int addFd(struct server *server, int fd, uint64_t tag)
{
    struct epoll_event event = { .events = EPOLLIN, .data.u64 = tag };
    return epoll_ctl(server->epollFd, EPOLL_CTL_ADD, fd, &event);
}

void serverDrop(struct server *server, struct client *client, enum close_reason reason)
{
    if (client->state == CLIENT_FREE || client->state == CLIENT_CLOSED)
        return;
    (void)epoll_ctl(server->epollFd, EPOLL_CTL_DEL, client->fd, NULL);
    server->bytesReceived += client->bytesReceived;
    server->bytesSent += client->bytesSent;
    --server->clientCount;
    ++server->disconnected;
    struct disconnect_record *record = &server->history[server->historyNext];
    *record = (struct disconnect_record){
        .id = client->id, .reason = reason, .at = time(NULL),
        .duration = server->now - client->connectedMono,
        .received = client->bytesReceived, .sent = client->bytesSent,
        .pending = bufferSize(&client->output)
    };
    memcpy(record->host, client->host, sizeof(record->host));
    memcpy(record->port, client->port, sizeof(record->port));
    server->historyNext = (server->historyNext + 1) % HISTORY_CAPACITY;
    if (server->historyCount < HISTORY_CAPACITY)
        ++server->historyCount;
    logEvent(server->verbose,
             "disconnect id=%" PRIu64 " reason=%s age=%jd rx=%" PRIu64 " tx=%" PRIu64 " pending=%zu",
             client->id, clientReason(reason), (intmax_t)record->duration,
             record->received, record->sent, record->pending);
    clientClose(client);
}

void serverWatch(struct server *server, struct client *client)
{
    struct epoll_event event = { .events = 0 };
    event.data.u64 = EVENT_CLIENT + (uint64_t)(client - server->clients);
    if (client->state == CLIENT_ACTIVE)
        event.events = EPOLLIN | EPOLLRDHUP;
    if (bufferSize(&client->output) != 0)
        event.events |= EPOLLOUT;
    if (epoll_ctl(server->epollFd, EPOLL_CTL_MOD, client->fd, &event) == -1) {
        logError("epoll client");
        serverDrop(server, client, CLOSE_EVENT);
    }
}

void serverStop(struct server *server)
{
    if (server->stopping)
        return;
    server->stopping = true;
    logEvent(server->verbose, "drain clients=%zu timeout=%d", server->clientCount, DRAIN_SECONDS);
    consoleClose(&server->console, server->epollFd);
    server->stopAt = server->now + DRAIN_SECONDS;
    if (server->listenFd != -1) {
        (void)epoll_ctl(server->epollFd, EPOLL_CTL_DEL, server->listenFd, NULL);
        (void)close(server->listenFd);
        server->listenFd = -1;
    }
    for (size_t index = 0; index < MAX_CLIENTS; ++index) {
        struct client *client = &server->clients[index];
        if (client->state == CLIENT_ACTIVE) {
            client->reason = CLOSE_SHUTDOWN;
            clientDrain(client, server->stopAt - DRAIN_SECONDS);
            serverWatch(server, client);
        }
    }
}

static struct client *freeClient(struct server *server)
{
    for (size_t count = 0; count < MAX_CLIENTS; ++count) {
        size_t index = server->nextSlot;
        server->nextSlot = (index + 1) % MAX_CLIENTS;
        if (server->clients[index].state == CLIENT_FREE)
            return &server->clients[index];
    }
    return NULL;
}

static void pauseListener(struct server *server, time_t now)
{
    (void)epoll_ctl(server->epollFd, EPOLL_CTL_DEL, server->listenFd, NULL);
    server->listenerPaused = true;
    server->acceptAfter = now + ACCEPT_RETRY_SECONDS;
}

static bool transientAcceptError(int error)
{
    return error == ECONNABORTED || error == ENETDOWN || error == EPROTO ||
           error == ENOPROTOOPT || error == EHOSTDOWN || error == ENONET ||
           error == EHOSTUNREACH || error == EOPNOTSUPP || error == ENETUNREACH;
}

static void acceptClients(struct server *server, time_t now)
{
    for (size_t count = 0; count < ACCEPT_BUDGET; ++count) {
        struct sockaddr_storage address;
        socklen_t length = sizeof(address);
        int fd = accept(server->listenFd, (struct sockaddr *)&address, &length);
        if (fd == -1) {
            if (errno == EINTR || transientAcceptError(errno))
                continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                logError("accept");
                /* Resource exhaustion must not produce an epoll busy loop. */
                pauseListener(server, now);
            }
            return;
        }
        struct client *client = server->clientCount < MAX_CLIENTS ? freeClient(server) : NULL;
        if (client == NULL || server->nextId == UINT64_MAX) {
            ++server->rejected;
            (void)close(fd);
            continue;
        }
        if (socketNonblock(fd) == -1) {
            logError("client flags");
            ++server->rejected;
            (void)close(fd);
            continue;
        }
        memset(client, 0, sizeof(*client));
        client->id = ++server->nextId;
        client->fd = fd;
        client->state = CLIENT_ACTIVE;
        client->connectedAt = time(NULL);
        client->connectedMono = now;
        client->lastActivity = now;
        if (socketPeer((struct sockaddr *)&address, length, client->host,
                       sizeof(client->host), client->port, sizeof(client->port)) != 0 ||
            addFd(server, fd, EVENT_CLIENT + (uint64_t)(client - server->clients)) == -1) {
            ++server->rejected;
            clientClose(client);
            continue;
        }
        ++server->clientCount;
        ++server->accepted;
        if (server->clientCount > server->peakClients)
            server->peakClients = server->clientCount;
        serverWatch(server, client);
        if (client->state == CLIENT_ACTIVE)
            logEvent(server->verbose, "connect id=%" PRIu64 " addr=[%s]:%s", client->id, client->host, client->port);
    }
}

static void clientEvent(struct server *server, struct client *client, uint32_t events, time_t now)
{
    if (client->state == CLIENT_FREE || client->state == CLIENT_CLOSED)
        return;
    if ((events & EPOLLERR) != 0) {
        int error = 0;
        socklen_t length = sizeof(error);
        if (getsockopt(client->fd, SOL_SOCKET, SO_ERROR, &error, &length) == -1)
            error = errno;
        if (error != 0) {
            serverDrop(server, client, error == ECONNRESET ? CLOSE_RESET : CLOSE_READ);
            return;
        }
    }
    if (client->state == CLIENT_ACTIVE && (events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) != 0) {
        if (clientRead(client, now) != 0) {
            serverDrop(server, client, client->reason);
            return;
        }
    }
    if ((events & EPOLLOUT) != 0 && clientWrite(client, now) != 0) {
        serverDrop(server, client, client->reason);
        return;
    }
    if ((events & EPOLLHUP) != 0 ||
        (client->state == CLIENT_DRAINING && bufferSize(&client->output) == 0)) {
        serverDrop(server, client, client->reason);
        return;
    }
    serverWatch(server, client);
}

static void maintainClients(struct server *server, time_t now)
{
    for (size_t index = 0; index < MAX_CLIENTS; ++index) {
        struct client *client = &server->clients[index];
        if (client->state == CLIENT_ACTIVE && now - client->lastActivity >= IDLE_SECONDS)
            serverDrop(server, client, CLOSE_IDLE);
        if (client->state == CLIENT_DRAINING) {
            if (bufferSize(&client->output) == 0)
                serverDrop(server, client, client->reason);
            else if (now >= client->drainUntil)
                serverDrop(server, client, CLOSE_DRAIN_TIMEOUT);
        }
        /* No slot can be reused while this batch still contains old events. */
        if (client->state == CLIENT_CLOSED)
            client->state = CLIENT_FREE;
    }
}

static int readSignal(struct server *server)
{
    for (;;) {
        struct signalfd_siginfo info;
        ssize_t count = read(server->signalFd, &info, sizeof(info));
        if (count == (ssize_t)sizeof(info)) {
            serverStop(server);
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 0;
        if (count >= 0)
            errno = EIO;
        return -1;
    }
}

static int eventLoop(struct server *server)
{
    struct epoll_event events[EVENT_BATCH];
    while (!server->stopping || server->clientCount != 0) {
        int count = epoll_wait(server->epollFd, events, EVENT_BATCH, EVENT_TIMEOUT_MS);
        if (count == -1) {
            if (errno == EINTR)
                continue;
            logError("epoll_wait");
            return -1;
        }
        time_t now = serverNow();
        if (now == (time_t)-1) {
            logError("clock_gettime");
            return -1;
        }
        server->now = now;
        /* Observe termination before accepting or reading this batch. */
        if (readSignal(server) != 0) {
            logError("signalfd read");
            return -1;
        }
        for (int index = 0; index < count; ++index) {
            uint64_t tag = events[index].data.u64;
            if (tag == EVENT_LISTEN && !server->stopping && !server->listenerPaused)
                acceptClients(server, now);
            else if (tag == EVENT_INPUT && server->console.reading && !server->stopping)
                consoleRead(&server->console, server->epollFd, operatorCommand, server);
            else if (tag == EVENT_OUTPUT && server->console.enabled)
                consoleWrite(&server->console, server->epollFd);
            else if (tag >= EVENT_CLIENT && tag - EVENT_CLIENT < MAX_CLIENTS)
                clientEvent(server, &server->clients[tag - EVENT_CLIENT], events[index].events, now);
        }
        maintainClients(server, now);
        if (server->stopping && now >= server->stopAt)
            break;
        if (!server->stopping && server->listenerPaused && now >= server->acceptAfter) {
            if (addFd(server, server->listenFd, EVENT_LISTEN) == -1) {
                logError("epoll listener");
                return -1;
            }
            server->listenerPaused = false;
        }
    }
    return 0;
}

static void cleanupServer(struct server *server)
{
    for (size_t index = 0; index < MAX_CLIENTS; ++index) {
        struct client *client = &server->clients[index];
        serverDrop(server, client, server->stopping ? CLOSE_DRAIN_TIMEOUT : CLOSE_SERVER_ERROR);
    }
    consoleClose(&server->console, server->epollFd);
    if (server->listenFd != -1)
        (void)close(server->listenFd);
    if (server->signalFd != -1)
        (void)close(server->signalFd);
    if (server->epollFd != -1)
        (void)close(server->epollFd);
    free(server);
}

int serverRun(const struct server_options *options)
{
    sigset_t signals;
    sigset_t oldSignals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &signals, &oldSignals) == -1) {
        logError("sigprocmask");
        return 1;
    }
    struct server *server = calloc(1, sizeof(*server));
    if (server == NULL) {
        logError("calloc");
        (void)sigprocmask(SIG_SETMASK, &oldSignals, NULL);
        return 1;
    }
    server->epollFd = server->listenFd = server->signalFd = -1;
    server->console.inputFlags = server->console.outputFlags = -1;
    server->verbose = options->verbose;
    int result = 1;
    server->epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (server->epollFd == -1) {
        logError("epoll_create1");
        goto done;
    }
    server->signalFd = signalfd(-1, &signals, SFD_NONBLOCK | SFD_CLOEXEC);
    if (server->signalFd == -1 || addFd(server, server->signalFd, EVENT_SIGNAL) == -1) {
        logError("signalfd");
        goto done;
    }
    server->listenFd = socketListen(options->address, options->port);
    if (server->listenFd == -1 || addFd(server, server->listenFd, EVENT_LISTEN) == -1) {
        logError("listen");
        goto done;
    }
    server->now = serverNow();
    if (server->now == (time_t)-1) {
        logError("clock_gettime");
        goto done;
    }
    server->startedAt = server->now;
    consoleInit(&server->console, server->epollFd);
    logEvent(server->verbose, "listen [%s]:%s", options->address, options->port);
    logEvent(server->verbose, "console=%s limit=%d idle=%d",
             server->console.enabled ? "on" : "off", MAX_CLIENTS, IDLE_SECONDS);
    result = eventLoop(server) == 0 ? 0 : 1;

done:
    cleanupServer(server);
    logEvent(options->verbose, "stop status=%d", result);
    (void)sigprocmask(SIG_SETMASK, &oldSignals, NULL);
    return result;
}
