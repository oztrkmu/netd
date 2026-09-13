#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "client.h"
#include "socket.h"

int main(void)
{
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(socketNonblock(pair[0]) == 0 && socketNonblock(pair[1]) == 0);
    int size = 1024;
    assert(setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) == 0);
    struct client client = { .fd = pair[0], .id = 1, .state = CLIENT_ACTIVE };
    char expected[OUTPUT_CAPACITY];
    for (size_t index = 0; index < sizeof(expected); ++index)
        expected[index] = (char)('a' + index % 26);
    assert(bufferAppend(&client.output, expected, sizeof(expected)) == 0);
    assert(clientWrite(&client, 1) == 0);
    assert(bufferSize(&client.output) > 0 && bufferSize(&client.output) < sizeof(expected));
    size_t queued = bufferSize(&client.output);
    assert(clientWrite(&client, 1) == 0 && bufferSize(&client.output) == queued);
    char actual[OUTPUT_CAPACITY];
    size_t received = 0;
    for (size_t attempt = 0; received < sizeof(actual) && attempt < 100; ++attempt) {
        ssize_t count = recv(pair[1], actual + received, sizeof(actual) - received, 0);
        if (count > 0)
            received += (size_t)count;
        else
            assert(count == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
        assert(clientWrite(&client, 2) == 0);
    }
    assert(received == sizeof(actual) && memcmp(actual, expected, received) == 0);
    assert(client.bytesSent == sizeof(expected));
    assert(send(pair[1], "PI", 2, 0) == 2);
    assert(clientRead(&client, 3) == 0 && client.input.length == 2);
    assert(send(pair[1], "NG\n", 3, 0) == 3);
    assert(shutdown(pair[1], SHUT_WR) == 0);
    assert(clientRead(&client, 4) == 0 && client.state == CLIENT_DRAINING);
    assert(client.bytesReceived == 5 && client.lastActivity == 4);
    assert(recv(pair[1], actual, sizeof(actual), 0) == 5);
    assert(memcmp(actual, "PONG\n", 5) == 0);
    clientClose(&client);
    clientClose(&client);
    assert(client.fd == -1 && client.state == CLIENT_CLOSED);
    assert(recv(pair[1], actual, sizeof(actual), 0) == 0);
    close(pair[1]);
    puts("client: partial write, EAGAIN, fragmentation, half-close ok");
    return 0;
}
