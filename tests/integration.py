#!/usr/bin/env python3
"""Loopback lifecycle tests; no third-party Python dependencies."""
import os
import select
import signal
import socket
import subprocess
import sys
import time

BINARY = os.path.abspath(sys.argv[1])


class Daemon:
    def __init__(self, address="127.0.0.1", verbose=False, extra=()):
        self.verbose = verbose
        self.logs = ""
        self.address = address
        family = socket.AF_INET6 if ":" in address else socket.AF_INET
        with socket.socket(family) as probe:
            probe.bind((address, 0))
            self.port = probe.getsockname()[1]
        self.process = subprocess.Popen(
            [BINARY, "-a", address, "-p", str(self.port)] + (["-v"] if verbose else []) + list(extra),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            bufsize=0,
        )
        self.output = b""
        self.clients = []
        deadline = time.monotonic() + 5
        while True:
            try:
                client = self.connect()
                client.close()
                break
            except ConnectionRefusedError:
                if self.process.poll() is not None or time.monotonic() > deadline:
                    self.finish(force=True)
                    raise AssertionError("daemon failed to start")
                time.sleep(0.01)

    def connect(self):
        client = socket.create_connection(("::1" if self.address == "::" else self.address, self.port), timeout=3)
        self.clients.append(client)
        return client

    def command(self, command):
        self.process.stdin.write(command.encode() + b"\n")

    def line(self):
        deadline = time.monotonic() + 5
        while b"\n" not in self.output:
            remaining = deadline - time.monotonic()
            assert remaining > 0 and select.select([self.process.stdout], [], [], remaining)[0], "console timeout"
            data = os.read(self.process.stdout.fileno(), 65536)
            assert data, "console EOF"
            self.output += data
        line, self.output = self.output.split(b"\n", 1)
        return line.decode()

    def finish(self, force=False):
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise AssertionError("shutdown timeout")
        for client in self.clients:
            client.close()
        error = self.process.stderr.read().decode()
        for pipe in (self.process.stdin, self.process.stdout, self.process.stderr):
            pipe.close()
        assert self.process.returncode == 0, error
        self.logs = error
        if not self.verbose:
            assert not error, error


def receive(client, expected):
    data = b""
    while len(data) < len(expected):
        chunk = client.recv(len(expected) - len(data))
        assert chunk, (data, expected)
        data += chunk
    assert data == expected, (data, expected)


def identity(client):
    client.sendall(b"INFO\n")
    data = b""
    while not data.endswith(b"\n"):
        data += client.recv(100)
    assert data.endswith(b" session=none\n"), data
    return int(data.split()[1].split(b"=")[1])


def closed(client):
    try:
        while client.recv(65536):
            pass
    except ConnectionResetError:
        pass


def basic():
    daemon = Daemon()
    try:
        first, second = daemon.connect(), daemon.connect()
        firstId, secondId = identity(first), identity(second)
        assert secondId > firstId
        first.sendall(b"PI")
        first.sendall(b"NG\r\nPING\nunknown\n")
        receive(first, b"PONG\nPONG\nERR command\n")
        second.sendall(b"PING\n" * 1000)
        receive(second, b"PONG\n" * 1000)
        daemon.command("help")
        for expected in ("help", "list", "info <id>", "send <id> <message>", "broadcast <message>", "kick <id>", "stats", "history", "quit"):
            assert daemon.line().startswith(expected + " ")
        daemon.command("list")
        assert f"id={firstId} " in daemon.line()
        assert f"id={secondId} " in daemon.line()
        assert daemon.line() == "clients=2"
        daemon.command(f"info {firstId}")
        assert f"id={firstId} " in daemon.line()
        assert "session=none connected=" in daemon.line()
        daemon.command(f"send {firstId} hello world")
        assert daemon.line() == f"queued id={firstId}"
        receive(first, b"MSG hello world\n")
        daemon.command("broadcast all")
        assert daemon.line() == "queued=2"
        receive(first, b"MSG all\n")
        receive(second, b"MSG all\n")
        for command in ("info -1", "send 0 x", "kick 18446744073709551616", "stats junk", "send 1"):
            daemon.command(command)
            assert daemon.line() == "error command"
        daemon.command(f"kick {firstId}")
        assert daemon.line() == f"closed id={firstId} reason=kick"
        closed(first)
        replacement = daemon.connect()
        assert identity(replacement) > secondId
        daemon.command(f"info {firstId}")
        assert daemon.line() == f"error client={firstId} not-found"
        second.shutdown(socket.SHUT_WR)
        closed(second)
        half = daemon.connect()
        half.sendall(b"PING\n" * 100)
        half.shutdown(socket.SHUT_WR)
        receive(half, b"PONG\n" * 100)
        closed(half)
        for data in (b"x" * 4097, b"PING\x00\n", b"\xff\n", b"PI\rNG\n"):
            invalid = daemon.connect()
            invalid.sendall(data)
            closed(invalid)
        exact = daemon.connect()
        exact.sendall(b"x" * 4096 + b"\nPING\n")
        receive(exact, b"ERR command\nPONG\n")
        partial = daemon.connect()
        partial.sendall(b"PI")
        partial.shutdown(socket.SHUT_WR)
        closed(partial)
        daemon.command("stats")
        assert "accepted=" in daemon.line()
        daemon.command("quit")
        daemon.process.wait(timeout=4)
        closed(replacement)
    finally:
        daemon.finish()
    print("integration: protocol, console, identity, half-close, quit ok")


def capacity():
    import resource
    limit, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
    if limit < 4096 and (hard == resource.RLIM_INFINITY or hard >= 4096):
        resource.setrlimit(resource.RLIMIT_NOFILE, (4096, hard))
        limit = 4096
    if limit < 1100:
        print("integration: capacity skipped (fd limit below 1100)")
        return
    daemon = Daemon()
    try:
        # Barrier: ensure the readiness probe has been reaped before filling slots.
        for _ in range(50):
            daemon.command("stats")
            if daemon.line().startswith("clients=0 "):
                break
            time.sleep(0.01)
        live = []
        for _ in range(1024):
            client = daemon.connect()
            identity(client)
            live.append(client)
        excess = daemon.connect()
        closed(excess)
        live[-1].sendall(b"PING\n")
        receive(live[-1], b"PONG\n")
        daemon.command("stats")
        line = daemon.line()
        assert "clients=1024 " in line and "rejected=1 " in line, line
        daemon.process.send_signal(signal.SIGINT)
        daemon.process.wait(timeout=4)
        closed(live[-1])
    finally:
        daemon.finish()
    print("integration: 1024 clients, capacity rejection, SIGINT ok")


def consoleEof():
    daemon = Daemon()
    try:
        daemon.process.stdin.close()
        client = daemon.connect()
        client.sendall(b"PING\n")
        receive(client, b"PONG\n")
        daemon.process.send_signal(signal.SIGTERM)
        daemon.process.wait(timeout=4)
        closed(client)
    finally:
        daemon.finish()
    print("integration: console EOF, SIGTERM ok")


def ipv6():
    try:
        with socket.socket(socket.AF_INET6) as probe:
            probe.bind(("::1", 0))
    except OSError:
        print("integration: IPv6 skipped (loopback unavailable)")
        return
    daemon = Daemon("::")
    try:
        client = daemon.connect()
        client.sendall(b"PING\n")
        receive(client, b"PONG\n")
        with socket.create_connection(("127.0.0.1", daemon.port), timeout=3) as ipv4:
            ipv4.sendall(b"PING\n")
            receive(ipv4, b"PONG\n")
    finally:
        daemon.finish()
    print("integration: IPv6 and dual-stack IPv4 ok")


def backpressure():
    daemon = Daemon()
    try:
        slow = daemon.connect()
        slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        slow.setblocking(False)
        healthy = daemon.connect()
        payload = b"PING\n" * 8192
        deadline = time.monotonic() + 10
        disconnected = False
        offset = 0
        while time.monotonic() < deadline:
            try:
                count = slow.send(payload[offset:])
                offset = (offset + count) % len(payload)
            except BlockingIOError:
                select.select([], [slow], [], 0.01)
            except (BrokenPipeError, ConnectionResetError):
                disconnected = True
                break
            healthy.sendall(b"PING\n")
            receive(healthy, b"PONG\n")
        assert disconnected, "slow reader was not disconnected at queue limit"
        # Leave stdout unread: the console must disable itself at its queue cap.
        daemon.process.stdin.write(b"help\n" * 819)
        healthy.sendall(b"PING\n")
        receive(healthy, b"PONG\n")
        daemon.process.send_signal(signal.SIGTERM)
        daemon.process.wait(timeout=4)
    finally:
        daemon.finish()
    print("integration: slow reader and stalled console isolation ok")


def restartAndCli():
    for arguments, code in ((["-h"], 0), (["-p", "0"], 2), (["-p", "65536"], 2),
                            (["-p", "-1"], 2), (["extra"], 2), (["-z"], 2)):
        result = subprocess.run([BINARY] + arguments, capture_output=True, timeout=3)
        assert result.returncode == code
    daemon = Daemon()
    port = daemon.port
    try:
        conflict = subprocess.run([BINARY, "-p", str(port)], capture_output=True, timeout=3)
        assert conflict.returncode == 1 and b"error listen:" in conflict.stderr
    finally:
        daemon.finish()
    # /dev/null disables the console, without disabling signal control.
    process = subprocess.Popen([BINARY, "-p", str(port)], stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        deadline = time.monotonic() + 3
        while True:
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=1) as client:
                    client.sendall(b"PING\n")
                    receive(client, b"PONG\n")
                break
            except ConnectionRefusedError:
                assert process.poll() is None and time.monotonic() < deadline
                time.sleep(0.01)
    finally:
        process.terminate()
        try:
            process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        assert process.returncode == 0, process.stderr.read()
        assert not process.stderr.read()
        process.stderr.close()
    print("integration: CLI, bind failure, immediate restart, headless operation ok")


def repeatedSignals():
    daemon = Daemon()
    try:
        daemon.connect()
        daemon.process.send_signal(signal.SIGSTOP)
        pid, status = os.waitpid(daemon.process.pid, os.WUNTRACED)
        assert pid == daemon.process.pid and os.WIFSTOPPED(status)
        daemon.process.send_signal(signal.SIGINT)
        daemon.process.send_signal(signal.SIGTERM)
        daemon.process.send_signal(signal.SIGCONT)
        daemon.process.wait(timeout=4)
    finally:
        daemon.finish()
    print("integration: simultaneous pending termination signals ok")


def reporting():
    daemon = Daemon(verbose=True)
    try:
        client = daemon.connect()
        assert not select.select([client], [], [], 0.05)[0], "unsolicited greeting"
        clientId = identity(client)
        client.sendall(b"bad\x00\n")
        closed(client)
        # Wait for the close record via a command barrier.
        daemon.command("history")
        history = []
        while True:
            line = daemon.line()
            if line.startswith("entries="):
                break
            history.append(line)
        assert any(line.startswith(f"id={clientId} ") and "reason=protocol" in line for line in history)
        killed = []
        for _ in range(40):
            client = daemon.connect()
            clientId = identity(client)
            killed.append(clientId)
            daemon.command(f"kick {clientId}")
            assert daemon.line() == f"closed id={clientId} reason=kick"
            closed(client)
        daemon.command("history")
        history = [daemon.line() for _ in range(32)]
        assert daemon.line() == "entries=32"
        assert [int(line.split()[0].split("=")[1]) for line in history] == killed[-32:]
        assert all("reason=kick" in line and "pending=0" in line for line in history)
        daemon.command("list")
        assert daemon.line() == "clients=0"
        daemon.command("stats")
        stats = dict(item.split("=", 1) for item in daemon.line().split())
        assert stats["clients"] == "0" and stats["limit"] == "1024"
        assert int(stats["closed"]) == int(stats["accepted"])
        assert int(stats["uptime"]) >= 0 and int(stats["peak"]) > 0
        active = daemon.connect()
        activeId = identity(active)
    finally:
        daemon.finish()
    assert "listen [127.0.0.1]:" in daemon.logs
    assert "console=on limit=1024 idle=300" in daemon.logs
    assert "reason=protocol" in daemon.logs and "reason=kick" in daemon.logs
    assert f"disconnect id={activeId} reason=shutdown" in daemon.logs
    assert daemon.logs.endswith("stop status=0\n")
    daemon = Daemon(verbose=True, extra=("-q",))
    daemon.finish()
    assert daemon.logs == "", "-q must override -v"
    print("integration: terse logs, quiet greeting, history bounds, reporting ok")


basic()
capacity()
consoleEof()
ipv6()
backpressure()
restartAndCli()
repeatedSignals()

reporting()
