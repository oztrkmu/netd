#include <stdint.h>
#include <string.h>
#include "command.h"

static const struct command_spec specs[] = {
    { CMD_HELP, "help", "help", "show commands" },
    { CMD_LIST, "list", "list", "show connected clients" },
    { CMD_INFO, "info", "info <id>", "show connection details" },
    { CMD_SEND, "send", "send <id> <message>", "queue a message" },
    { CMD_BROADCAST, "broadcast", "broadcast <message>", "queue for all active clients" },
    { CMD_KICK, "kick", "kick <id>", "close a connection" },
    { CMD_STATS, "stats", "stats", "show server counters" },
    { CMD_HISTORY, "history", "history", "show recent disconnects" },
    { CMD_QUIT, "quit", "quit", "drain and stop" }
};

const struct command_spec *commandSpecs(size_t *count)
{
    *count = sizeof(specs) / sizeof(specs[0]);
    return specs;
}

int parseId(const char *text, uint64_t *id)
{
    uint64_t value = 0;
    if (*text == '\0')
        return -1;
    for (; *text != '\0'; ++text) {
        if (*text < '0' || *text > '9')
            return -1;
        unsigned int digit = (unsigned int)(*text - '0');
        if (value > (UINT64_MAX - digit) / 10)
            return -1;
        value = value * 10 + digit;
    }
    if (value == 0)
        return -1;
    *id = value;
    return 0;
}

static char *skipSpace(char *text)
{
    while (*text == ' ' || *text == '\t')
        ++text;
    return text;
}

static char *takeWord(char **cursor)
{
    char *word = skipSpace(*cursor);
    char *end = word;
    while (*end != '\0' && *end != ' ' && *end != '\t')
        ++end;
    if (*end != '\0')
        *end++ = '\0';
    *cursor = skipSpace(end);
    return word;
}

struct command commandParse(char *line)
{
    struct command command = { .kind = CMD_INVALID };
    char *cursor = line;
    char *name = takeWord(&cursor);
    size_t count;
    const struct command_spec *commands = commandSpecs(&count);
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(name, commands[index].name) == 0) {
            command.kind = commands[index].kind;
            break;
        }
    }
    if (command.kind == CMD_INFO || command.kind == CMD_SEND || command.kind == CMD_KICK) {
        char *idText = takeWord(&cursor);
        if (parseId(idText, &command.id) != 0)
            command.kind = CMD_INVALID;
    }
    if (command.kind == CMD_SEND || command.kind == CMD_BROADCAST) {
        command.message = cursor;
        if (*cursor == '\0')
            command.kind = CMD_INVALID;
    } else if (*cursor != '\0') {
        command.kind = CMD_INVALID;
    }
    return command;
}
