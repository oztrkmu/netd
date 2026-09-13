# netd

Small TCP networking daemon written in C.

Linux first.
KISS. No unnecessary dependencies or abstractions.

Built to learn and explore network and systems programming close to the OS.

## build

sh
make

## run

sh
./build/netd

<<<<<<< HEAD
## status
Work in progress.
See `SPEC.md` and `ROADMAP.md` for details.
=======
```sh
build/netd                   # 127.0.0.1:8080
build/netd -a ::1 -p 9000 -v  # IPv6 loopback, event logs
build/netd -h
```

`-a` accepts a numeric IPv4/IPv6 address; `-p` accepts ports 1–65535. One listening
socket is opened. `-a 0.0.0.0` exposes IPv4 interfaces; `-a ::` requests a Linux
dual-stack wildcard listener. Default loopback binding keeps the initial service
local. `-q` disables event logs, including a preceding `-v`.

In two other terminals, open persistent clients:

```sh
nc 127.0.0.1 8080
```

Type `PING` or `INFO` in either client. Inspect connections with:

```sh
ss -tn 'sport = :8080'
```

Enter operator commands in the **daemon's terminal**:

```text
help
list
info 1
send 1 hello world
broadcast maintenance soon
kick 1
stats
quit
```

Use the IDs returned by `list`; descriptors are never command identities.
A list entry looks like `id=1 fd=7 addr=[127.0.0.1]:51822 state=active`.
`info` also reports the connection's Unix timestamp, idle seconds, traffic
counters, connection age, and queued bytes. `send` returning `queued id=1` means queued, not acknowledged
by the peer. `kick` returns `closed id=1 reason=kick`. `list` ends with
`clients=<count>`, including `clients=0` when empty. `broadcast` reports how many active connections accepted the message.

`stats` includes uptime in seconds, peak simultaneous clients and total closed
connections. `history` shows the most recent 32 disconnects, oldest first, then
`entries=<count>`. Each record includes the peer address, close reason, Unix close
time, connection age in seconds, traffic counters and unsent queue size. History
is bounded in memory and is lost on restart; it is not a session registry.

With `-v`, lifecycle output stays short:

```text
listen [127.0.0.1]:8080
console=on limit=1024 idle=300
connect id=1 addr=[127.0.0.1]:51822
disconnect id=1 reason=peer age=12 rx=5 tx=5 pending=0
drain clients=0 timeout=2
stop status=0
```

Reasons distinguish `peer`, `reset`, `protocol`, `incomplete`, `backpressure`,
`idle`, `kick`, `shutdown`, `drain-timeout`, and internal I/O/event errors.
`peer` means a TCP EOF/hangup, not proof that the remote application deliberately
quit. TCP does not report a desire to reconnect, and matching IP addresses do not
establish identity. No greeting is sent when a client connects: protocol responses
are requested explicitly. `-h` explains CLI options; console `help` explains each
operator command from the same table used by the command parser.

The console requires epoll-capable stdin and stdout (terminals, pipes or sockets).
It disables itself when either is a regular file or `/dev/null`, or when its
64 KiB output queue overflows. To script commands, use pipes, for example:

```sh
printf 'stats\nquit\n' | build/netd | cat
```

EOF on stdin detaches input without stopping the server. Invalid/oversized console
frames detach input; ordinary command syntax errors report `error command` and
allow the next command. For headless operation use `build/netd </dev/null` and stop
with SIGINT or SIGTERM. There is no daemonization, pidfile, or remote admin socket.

## Wire protocol

Requests and replies are ASCII lines terminated by LF; requests may use CRLF.
The maximum request length is 4,096 bytes before LF, including an optional CR.
Tabs and printable ASCII are accepted; NUL, other controls, non-ASCII bytes,
embedded CR, and oversized frames disconnect the transport. A partial final line
at EOF is invalid. Multiple requests can share a read or span many reads.

| Request | Response |
| --- | --- |
| `PING` | `PONG` |
| `INFO` | `INFO id=<connection-id> session=none` |
| Other complete line (including empty) | `ERR command` |

Operator messages arrive asynchronously as `MSG <message>`. Network peers cannot
execute operator commands. This version has no authentication, encryption,
application payload processing, or message-delivery guarantees.

## Architecture and limits

- `main.c`: CLI and process setup; `server.c`: epoll, acceptance, signals, timers,
  and resource ownership. Linux `signalfd` delivers blocked termination signals
  inside the event loop; no asynchronous handler touches connection state.
- `socket.c`: numeric address resolution, socket flags, bind/listen and peer names.
  `client.c`: visible recv/send loops, half-close, reply dispatch and counters.
- `protocol.c` and `buffer.c`: bounded line assembly and partial-write queue.
  `command.c`: parsing; `operator.c`: command execution; `console.c`: bounded
  nonblocking console I/O; `log.c`: compact diagnostics.

The server owns one heap allocation containing 1,024 stable client slots, roughly
21 MiB of userspace storage at capacity, plus bounded console storage. No allocation
occurs while accepting or processing client messages. Closed slots become reusable
only after the current epoll batch finishes. The fd is transport state; a monotonic
64-bit connection ID is unique within one process lifetime and never reused there.

`include/config.h` defines capacity, the 16 KiB per-client output queue, framing,
backlog, accept/I/O budgets, a 300-second inactivity timeout, and a two-second
shutdown drain. Excess connections are accepted and closed. A full output queue
closes that client. Accept resource exhaustion pauses the listener briefly to
avoid spinning. Kernel socket buffers and process fd limits are additional OS
limits; raise the fd limit above the configured client count when needed.

Shutdown closes the listener, disables commands and new reads, drains already
queued replies until the deadline, then shuts down and closes transports. Peer
write-half-close also drains complete replies. Shutdown deadlines and idle expiry
use monotonic time with a one-second epoll timeout; delivery is not guaranteed.
A `kick`, malformed frame, idle expiry, or queue overflow closes immediately.

Diagnostics use best-effort nonblocking writes and may be dropped when stderr is
full. As with ordinary Unix programs, writing diagnostics to a regular file can
still incur filesystem latency. The server does not take ownership of inherited
stdio descriptors and restores changed status flags when it exits.

## Status and next steps

The epoll core, dual-stack addressing, limits, console and regression tests are
implemented. Connection IDs reset across process restarts. The `sessionId` field
is explicitly unassigned: there is **no reconnect, session resumption, or state
persistence yet**. TCP cannot resume a dead socket.

Next steps are a real application protocol, authenticated logical sessions,
client reconnect/backoff, optional logical-state storage, and measurement before
further event-loop optimization. See [SPEC.md](SPEC.md) for ownership and extension
contracts and [ROADMAP.md](ROADMAP.md) for deferred work.
>>>>>>> 79f004e (update netd)
