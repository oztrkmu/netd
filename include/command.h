#ifndef NETD_COMMAND_H
#define NETD_COMMAND_H
#include <stddef.h>
#include <stdint.h>

enum command_kind { CMD_INVALID, CMD_HELP, CMD_LIST, CMD_INFO, CMD_SEND,
                    CMD_BROADCAST, CMD_KICK, CMD_STATS, CMD_QUIT, CMD_HISTORY };
struct command_spec {
    enum command_kind kind;
    const char *name;
    const char *usage;
    const char *description;
};
const struct command_spec *commandSpecs(size_t *count);
struct command {
    enum command_kind kind;
    uint64_t id;
    const char *message;
};
int parseId(const char *text, uint64_t *id);
struct command commandParse(char *line);
#endif
