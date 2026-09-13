#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "buffer.h"
#include "command.h"
#include "protocol.h"

static int countFrame(void *context, const char *line)
{
    int *count = context;
    assert(strcmp(line, "PING") == 0);
    ++*count;
    return 0;
}

int main(void)
{
    uint64_t id = 0;
    assert(parseId("18446744073709551615", &id) == 0 && id == UINT64_MAX);
    const char *bad[] = { "", "0", "-1", "+1", "1x", "18446744073709551616" };
    for (size_t index = 0; index < sizeof(bad) / sizeof(bad[0]); ++index)
        assert(parseId(bad[index], &id) == -1);
    char sendLine[] = "  send 42 hello world";
    struct command command = commandParse(sendLine);
    assert(command.kind == CMD_SEND && command.id == 42);
    assert(strcmp(command.message, "hello world") == 0);
    char invalid[] = "kick 4 extra";
    assert(commandParse(invalid).kind == CMD_INVALID);
    size_t commandCount;
    const struct command_spec *commands = commandSpecs(&commandCount);
    for (size_t index = 0; index < commandCount; ++index) {
        char commandLine[128];
        const char *arguments = "";
        if (commands[index].kind == CMD_INFO || commands[index].kind == CMD_KICK)
            arguments = " 1";
        else if (commands[index].kind == CMD_SEND)
            arguments = " 1 hello";
        else if (commands[index].kind == CMD_BROADCAST)
            arguments = " hello";
        (void)snprintf(commandLine, sizeof(commandLine), "%s%s", commands[index].name, arguments);
        assert(commandParse(commandLine).kind == commands[index].kind);
        assert(*commands[index].usage != '\0' && *commands[index].description != '\0');
    }
    struct frame frame = {0};
    int count = 0;
    assert(frameFeed(&frame, "PI", 2, countFrame, &count) == 0);
    assert(frameFeed(&frame, "NG\r\nPING\n", 9, countFrame, &count) == 0 && count == 2);
    assert(frameFeed(&frame, "\0", 1, countFrame, &count) == -1);
    memset(&frame, 0, sizeof(frame));
    char large[MAX_MESSAGE_SIZE + 1];
    memset(large, 'a', sizeof(large));
    assert(frameFeed(&frame, large, MAX_MESSAGE_SIZE, countFrame, &count) == 0);
    assert(frameFeed(&frame, large, 1, countFrame, &count) == -1);
    struct buffer buffer = {0};
    char fill[OUTPUT_CAPACITY];
    memset(fill, 'x', sizeof(fill));
    assert(bufferAppend(&buffer, fill, sizeof(fill)) == 0);
    assert(bufferAppend(&buffer, "x", 1) == -1);
    bufferConsume(&buffer, 100);
    assert(bufferAppend(&buffer, fill, 100) == 0);
    assert(bufferSize(&buffer) == OUTPUT_CAPACITY);
    bufferConsume(&buffer, OUTPUT_CAPACITY);
    assert(buffer.start == 0 && buffer.end == 0);
    puts("core: ok");
    return 0;
}
