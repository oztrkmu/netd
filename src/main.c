#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include "command.h"
#include "config.h"
#include "server.h"

static void usage(FILE *stream)
{
    fputs("usage: netd [-a address] [-p port] [-v|-q]\n", stream);
}

int main(int argc, char **argv)
{
    struct server_options options = {
        .address = DEFAULT_ADDRESS, .port = DEFAULT_PORT, .verbose = false
    };
    int option;
    while ((option = getopt(argc, argv, "a:p:vqh")) != -1) {
        switch (option) {
        case 'a': options.address = optarg; break;
        case 'p': options.port = optarg; break;
        case 'v': options.verbose = true; break;
        case 'q': options.verbose = false; break;
        case 'h':
            usage(stdout);
            fputs("  -a address  numeric listen address (default 127.0.0.1)\n"
                  "  -p port     listen port (default 8080)\n"
                  "  -v          short lifecycle logs on stderr\n"
                  "  -q          quiet (default; errors still reported)\n"
                  "  -h          show help\n"
                  "console: help, list, info, send, broadcast, kick, stats, history, quit\n", stdout);
            return 0;
        default: usage(stderr); return 2;
        }
    }
    uint64_t port;
    if (optind != argc || parseId(options.port, &port) != 0 || port > 65535) {
        usage(stderr);
        return 2;
    }
    /* Also protects the operator pipe and diagnostics if their reader exits. */
    struct sigaction action = { .sa_handler = SIG_IGN };
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPIPE, &action, NULL) != 0) {
        perror("error sigaction");
        return 1;
    }
    int errorFlags = fcntl(STDERR_FILENO, F_GETFL);
    if (errorFlags != -1 && fcntl(STDERR_FILENO, F_SETFL, errorFlags | O_NONBLOCK) == -1) {
        perror("error stderr flags");
        return 1;
    }
    int result = serverRun(&options);
    if (errorFlags != -1)
        (void)fcntl(STDERR_FILENO, F_SETFL, errorFlags);
    return result;
}
