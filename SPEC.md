# netd core specification

## Scope

Linux-first, C17, one foreground process and one thread. POSIX sockets plus epoll
and signalfd. Minimal fixed limits, no event-backend framework, worker pool, or
storage engine. Variables/functions use camelCase; structs use conventional C
names. Public headers describe the small module boundaries; `server_internal.h`
is shared only by server/operator/console implementation files.

Dependencies:

```text
main -> server -> socket
               -> client -> protocol, buffer
               -> operator -> command, client, console
               -> console -> protocol
               -> log
```

`server_internal.h` defines stable event tags and the server-owned state. Socket,
protocol, buffer and command helpers do not own the event loop. Console helpers
receive an epoll fd explicitly; there is no global server singleton.

## Ownership and event lifetime

`serverRun` allocates and frees the server, and owns epoll/listen/signal fds and all
client slots. `acceptClients` owns each new fd until it attaches it to a slot; all
failure branches close it. A client slot owns its fd and inline input/output
buffers. Nothing else frees a slot or shares its buffers. Inherited stdio fds are
borrowed; altered file status flags are restored.

Slots transition `free -> active -> draining -> closed -> free`; errors/kick may
transition directly from active to closed. Closure unregisters the fd before
shutdown/close, accounts traffic once, and marks the slot closed. Reclamation runs
only after the epoll batch. Thus an event from an earlier connection cannot target
a replacement in the same slot. Closing fds is never retried on Linux.

Epoll tags identify internal event sources or stable slot indexes. They are not
public client identities. Operator commands search live slots by a monotonically
increasing 64-bit ID. The process rejects new connections at ID exhaustion instead
of wrapping. There is no fd-to-identity assumption.

## I/O and overload policy

Sockets are nonblocking and close-on-exec. One listener uses SO_REUSEADDR. IPv6
listeners explicitly request dual-stack behavior; the chosen local address still
controls what is reachable. Numeric address/service resolution avoids DNS delays.

Level-triggered readiness allows bounded accept/read/write work without losing a
notification. Reads feed a bounded line decoder; complete frames dispatch
immediately, and partial frames remain in the client slot. Sends consume only the
bytes accepted by the kernel. EINTR retries, EAGAIN returns to epoll, EOF drains
complete replies, and fatal socket errors close the client. MSG_NOSIGNAL protects
socket writes; SIGPIPE is ignored for borrowed console/log pipes as well.
EPOLLOUT is armed only for pending output.

All queues are bounded. A peer that exhausts its output queue is disconnected,
even if it sent syntactically valid requests. This is deliberate overload policy;
there is no promise to accept an arbitrarily large pipelined request burst.
Acceptance is bounded per event, and fd/memory exhaustion temporarily removes the
listener from epoll. Capacity rejection closes the newly accepted fd. Client idle
expiry uses either-direction successful I/O, not mere readiness notifications.

The console uses the same framing bounds and rejects invalid frames by detaching
input. Syntax errors do not detach it. Stalled output disables the entire console;
server signal handling and network I/O continue. Script writers must keep reading
stdout, and terminate each command with LF. Console shutdown discards any remaining
console output; network output has a separate drain deadline.

SIGINT/SIGTERM are blocked before sockets are opened and consumed using signalfd.
Shutdown disables acceptance and reads immediately, keeps writable clients until
queues empty or the deadline expires, then closes everything. No transport is
persisted. Fatal event-loop/startup errors clean up and return nonzero.

## Connection versus logical session (future contract)

`client.id`, fd, peer address, counters and framing buffers describe one transport
connection. `client.sessionId == 0` explicitly means no logical session is bound.
`INFO` exposes `session=none`; clients must not treat a connection ID as a resume
credential. Request dispatch in `client.c` is the place to add an application
handshake once its semantics are defined.

A future session store should own application state independently of client slots.
A verified handshake can attach a session handle to a new transport; disconnect
should detach that handle without necessarily deleting the session. Define expiry,
authentication, simultaneous attachment policy, and explicit sequence/acknowledgment
semantics before claiming resumption or delivery guarantees.

A reconnecting client must open a new TCP connection, use bounded exponential
backoff with jitter, authenticate, and negotiate which logical operations can be
resumed or retried. Exactly-once behavior does not follow from reconnecting.

Optional restart persistence may store a versioned logical session identifier,
expiry, application state, and protocol progress. Never store fds, epoll tags,
pointers, raw transport buffers, or monotonic timestamps as recoverable connections.
A small atomic snapshot could be sufficient initially; durability, corruption
handling, and token protection need tests before it is enabled. No storage module
or dormant resume command is included in this version.

## Deliberate limits

The local console is trusted process input; the network is untrusted. The loopback
default is useful for development. External deployment needs an application threat
model, authentication and transport security. There are no per-address rate limits,
absolute frame-assembly deadlines, keepalive negotiation or application heartbeats.

Slot allocation, commands and deadline scans use bounded linear scans of 1,024
slots. This keeps ownership visible and avoids a premature hash table or timer
wheel. Larger limits should follow profiling, memory budgeting, fd-limit planning,
and fairness tests. There is no claim of benchmarked throughput or production
readiness.
