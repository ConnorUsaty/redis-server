# Redis-Compatible Key-Value Server

A C++20 key-value server built to compare event-driven and thread-per-client architectures under identical workloads, with benchmarked latency and throughput for both.

## Performance Results

All benchmarks run over loopback via Google Benchmark (methodology below):

| Metric | Event loop | Thread-per-client |
|---|---|---|
| Median round-trip latency (single client) | **43.6 µs** | 1.1 ms |
| Sustained throughput (16 connections) | **68k+ req/s** | — |

The event-driven design achieves a **~25x median latency improvement** over the threaded baseline.

![image](https://github.com/user-attachments/assets/a3289e63-0723-4551-9b12-b42670b8d706)

## Architecture

**Event-driven server** (`ServerEventLoop`) — a single-threaded event loop multiplexes all client connections with `poll(2)` and non-blocking sockets. One thread handles all I/O readiness, parsing, and command execution, eliminating context switches, synchronization, and shared mutable state from the hot path. This is the same architectural choice made by Redis itself, and the benchmark quantifies why: coordination overhead in the thread-per-client design costs ~25x in median latency.

**Threaded server** (`ServerThreaded`) — a thread-per-client baseline used as the comparison point.

Both servers share connection setup, protocol parsing, and command dispatch through a common `ServerBase`, so the benchmark isolates the architectural difference.

## Key Design Decisions

- **Custom `Buffer` struct** — 64-byte cache-line-aligned (`alignas(64)` + `aligned_alloc`), with O(1) append, consume, and clear; avoids repeated allocation and shifting during request parsing
- **Request pipelining** — each readiness event drains the read buffer in a loop, parsing and answering every complete request it contains, amortizing syscall cost across pipelined requests
- **Length-prefixed binary protocol** over TCP — `msg_len | n_strs | len_1 | str_1 | ...` framing for reliable message boundaries on a stream socket
- **Non-blocking sockets** throughout (`O_NONBLOCK` via `fcntl`); the event loop never stalls on a slow client
- **`std::unordered_map` storage** with `get`, `set`, and `del` commands

## Benchmark Methodology

- **Latency**: single client issuing blocking round-trips (`get` request → response), 1,000-iteration warmup, per-iteration manual timing reported to Google Benchmark; median reported
- **Throughput**: 1 / 4 / 8 / 16 concurrent client connections issuing requests for a fixed 1-second window per iteration; total completed ops reported
- Server runs in-process on a separate thread; all traffic over loopback

## Build & Run

Build everything (servers, client, tests, benchmarks):

```bash
./build.sh
```

Start server and client together:

```bash
./run_client_and_server.sh
```

Or run them in separate terminals:

```bash
cd build/
./server_event-loop.exe   # terminal 1
./client.exe              # terminal 2
```

## Tests & Benchmarks

```bash
cd build/
ctest                  # all unit tests
./buffer_unit_test     # Buffer struct tests
./servers_unit_test    # server behavior tests
./servers_benchmark    # latency + throughput benchmarks
```

## Future Work

- Replace `poll(2)` with `epoll`, then `io_uring`, and re-benchmark at higher connection counts
- Add a worker thread pool behind the event loop (SPSC queue handoff) and measure where the crossover point is
- Convert wire-format integers to network byte order for cross-platform correctness
- Extend the command set and add RESP protocol compatibility
