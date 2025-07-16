# High-Performance Key-Value Store

A C++20 implementation comparing event-driven vs threaded architectures. Optimized for low-latency and high-throughput.

## Performance Results
- **Latency**: 43.6μs (event-loop) vs 1.1ms (threaded) - 25x improvement
- **Throughput**: 68k ops/sec with 16 concurrent clients (event-loop)

  
![image](https://github.com/user-attachments/assets/a3289e63-0723-4551-9b12-b42670b8d706)


## Key Optimizations
- Custom cache-friendly, memory-aligned Buffer class with O(1) reads, appends, consumes, and clears
- Concurrent lock-free event loop that uses non-blocking I/O and a single thread to handle multiple clients concurrently

## Future Improvements
- epoll/io_uring for better scalability for ServerEventLoop
- Thread pool for ServerThreaded

## Usage
To build all .exe (test and usage) run `./build.sh`

Then to easily start the server and client run `./run_client_and_server.sh`

To run the server and client one by one in seperate terminals:
```
cd build/
./server_event-loop.exe
```
And in your second terminal:
```
cd build/
./client.exe
```

## Tests
To build all .exe (test and usage) run `./build.sh`

To run all unit tests:
```
cd build/
ctest
```

To run an individual unit test `cd build/` then choose between `./servers_unit_test` and `./buffer_unit_test`

To run the benchmarks `cd build/` and then `./servers_benchmark`
