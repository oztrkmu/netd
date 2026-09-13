#include <stdint.h>
#include <string.h>
#include "command.h"

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
    const char *names[] = { "", "help", "list", "info", "send",
                            "broadcast", "kick", "stats", "quit", "history" };
    for (size_t index = 1; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (strcmp(name, names[index]) == 0) {
            command.kind = (enum command_kind)index;
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
