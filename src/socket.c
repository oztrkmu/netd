#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include "config.h"
#include "socket.h"

int socketNonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        return -1;
    flags = fcntl(fd, F_GETFD);
    if (flags == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
        return -1;
    return 0;
}

int socketListen(const char *address, const char *port)
{
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM,
                              .ai_flags = AI_NUMERICHOST | AI_NUMERICSERV };
    struct addrinfo *addresses = NULL;
    if (getaddrinfo(address, port, &hints, &addresses) != 0) {
        errno = EINVAL;
        return -1;
    }
    int listenFd = -1;
    int savedError = EADDRNOTAVAIL;
    for (struct addrinfo *entry = addresses; entry != NULL; entry = entry->ai_next) {
        int fd = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (fd == -1) {
            savedError = errno;
            continue;
        }
        int enabled = 1;
        int dualStack = 0;
        if (socketNonblock(fd) == 0 &&
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) == 0 &&
            (entry->ai_family != AF_INET6 ||
             setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &dualStack, sizeof(dualStack)) == 0) &&
            bind(fd, entry->ai_addr, entry->ai_addrlen) == 0 &&
            listen(fd, LISTEN_BACKLOG) == 0) {
            listenFd = fd;
            break;
        }
        savedError = errno;
        close(fd);
    }
    freeaddrinfo(addresses);
    if (listenFd == -1)
        errno = savedError;
    return listenFd;
}

int socketPeer(const struct sockaddr *address, socklen_t length,
               char *host, size_t hostSize, char *port, size_t portSize)
{
    return getnameinfo(address, length, host, (socklen_t)hostSize,
                       port, (socklen_t)portSize, NI_NUMERICHOST | NI_NUMERICSERV);
}
