# Roadmap

## Implemented: Linux daemon core

- C17, strict warnings, separate release/debug/sanitizer builds.
- IPv4/IPv6 listener, level-triggered epoll, nonblocking bounded I/O.
- Connection metadata and process-local IDs, local operator commands.
- Framing limits, output backpressure, capacity rejection, idle expiry.
- Signal shutdown, half-close draining, fd-exhaustion retry delay.
- Parser/buffer/socket tests and live multi-client integration tests.

## Next: application semantics

- Select a useful application protocol; version it before external compatibility
  commitments. Keep framing separate from request handling.
- Define authentication and deployment requirements; evaluate TLS when needed.
- Add fault-injection tests for allocation/fd exhaustion and timing policies.
- Measure latency, memory, CPU and fairness with persistent clients and churn.

## Later: logical sessions

- Authenticated session handshake with independent session ownership and expiry.
- A client implementation with reconnect backoff and jitter.
- Explicit retry, sequence and acknowledgment rules before session resumption.
- Optional versioned logical-state snapshots and restart/corruption tests.

## Only when justified

- Finer-grained rate limits, absolute frame deadlines and heartbeat policy.
- Indexed lookups, timer scheduling or multiple event loops based on measurements.
- Other Unix event backends if a real portability requirement appears.

Persistence, TLS, session recovery and performance targets remain future work.
