We are building a serious C networking project called netd.

Do not start implementing features yet.

First inspect the entire repository and read:

SPEC.md
ROADMAP.md
README.md

The project must follow these rules:

- C11/C17
- Unix/Linux style
- camelCase function and variable naming
- KISS
- small focused functions
- explicit error handling
- no hidden global state unless justified
- no blocking I/O in the event loop
- bounded memory usage
- no unbounded queues
- Linux first
- portable architecture for epoll/kqueue/IOCP later
- IPv4 and IPv6 architecture
- production-grade error handling
- clean ownership/lifetime rules
- compile with strict warnings

Do not blindly add abstractions.
Every abstraction must solve a real networking or portability problem.

Our purpose is to deeply learn systems and network programming,
so explain important design decisions while implementing them.

First task:

Review the planned architecture and propose the directory/module
dependency graph.

Do not write implementation code until the architecture is coherent.
