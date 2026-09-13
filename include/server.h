#ifndef NETD_SERVER_H
#define NETD_SERVER_H
#include <stdbool.h>
struct server_options {
    const char *address;
    const char *port;
    bool verbose;
};
int serverRun(const struct server_options *options);
#endif
