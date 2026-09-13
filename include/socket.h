#ifndef NETD_SOCKET_H
#define NETD_SOCKET_H
#include <stddef.h>
#include <sys/socket.h>
int socketNonblock(int fd);
int socketListen(const char *address, const char *port);
int socketPeer(const struct sockaddr *address, socklen_t length,
               char *host, size_t hostSize, char *port, size_t portSize);
#endif
