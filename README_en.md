# High-Performance HTTP/1.1 Server

English | [中文](README.md)

A high-performance HTTP/1.1 server built from scratch in C++17 on Linux, using only system calls (epoll / socket / timerfd) with zero third-party network library dependencies.

Inspired by [muduo](https://github.com/chenshuo/muduo) (by Chen Shuo), implementing a **Main-Sub Reactor** multi-threaded architecture.

## Features

- **Main-Sub Reactor Architecture** — MainLoop accepts connections, SubLoop(s) handle all IO
- **epoll (ET mode)** — Edge-triggered epoll for minimal system call overhead
- **Multi-threaded** — Configurable worker thread pool, round-robin connection distribution
- **HTTP/1.1 Parsing** — State machine parser supporting half-packet / sticky-packet scenarios
- **Async Logging** — Double-buffered background thread logging with log-level filtering
- **Idle Connection Timeout** — Periodic detection and automatic cleanup of idle connections
- **Timer System** — timerfd-based timer queue, integrated into the event loop
- **Zero External Dependencies** — Only Linux system calls + C++17 standard library (Google Test for testing)

## Architecture

```
┌─────────────────────────────────────────────┐
│                  Main Reactor                │
│   MainLoop (single thread) + Acceptor        │
│   Listens on port, accepts new connections    │
└────────────────┬────────────────────────────┘
                 │ Round-Robin distribution
    ┌────────────┼────────────┐
    ▼            ▼            ▼
┌────────┐  ┌────────┐  ┌────────┐
│SubLoop1│  │SubLoop2│  │SubLoop3│   Sub Reactors
│(Thread1│  │(Thread2│  │(Thread3│   One EventLoop per thread
└────┬───┘  └───┬────┘  └───┬────┘
     │          │            │
  Connection Connection  Connection
  HTTP Parse  HTTP Parse  HTTP Parse
  Read/Write  Read/Write  Read/Write
```

## Project Structure

```
http-server/
├── CMakeLists.txt
├── README.md
├── DESIGN.md                      # Detailed design document
├── docs/
│   ├── phase1_log.md              # Phase 1: Single-threaded Reactor
│   ├── phase2_log.md              # Phase 2: HTTP Protocol Layer
│   ├── phase3_log.md              # Phase 3: Multi-threaded Reactor
│   └── phase4_log.md              # Phase 4: Stability & Observability
│
├── src/
│   ├── core/                      # Network Layer
│   │   ├── inet_address.h/cpp     # sockaddr_in wrapper
│   │   ├── buffer.h/cpp           # Application-layer read/write buffer (muduo-style)
│   │   ├── poller.h/cpp           # epoll wrapper
│   │   ├── channel.h/cpp          # fd event dispatcher
│   │   ├── event_loop.h/cpp       # Reactor core event loop
│   │   ├── acceptor.h/cpp         # Port listener, accepts new connections
│   │   ├── tcp_connection.h/cpp   # TCP connection management
│   │   ├── tcp_server.h/cpp       # Server entry point (Main-Sub Reactor)
│   │   ├── event_loop_thread.h/cpp       # Runs EventLoop in a separate thread
│   │   └── event_loop_thread_pool.h/cpp  # Sub Reactor thread pool
│   │
│   ├── http/                      # HTTP Layer
│   │   ├── http_request.h/cpp     # HTTP request data model
│   │   ├── http_response.h/cpp    # HTTP response builder & serializer
│   │   ├── http_parser.h/cpp      # Stateless request-line & header parser
│   │   ├── http_context.h/cpp     # Per-connection state machine
│   │   └── http_server.h/cpp      # HTTP server wrapping TcpServer
│   │
│   ├── utils/                     # Utilities
│   │   ├── timer.h/cpp            # Single timer task
│   │   ├── timer_queue.h/cpp      # Timer management (timerfd-based)
│   │   └── logger.h/cpp           # Async logging (double-buffered)
│   │
│   └── main.cpp                   # Entry point
│
└── tests/                         # Unit Tests (Google Test)
    ├── test_inet_address.cpp
    ├── test_buffer.cpp
    ├── test_poller.cpp
    ├── test_channel.cpp
    ├── test_event_loop.cpp
    ├── test_acceptor_connection.cpp
    ├── test_http_request_response.cpp
    ├── test_http_parser.cpp
    ├── test_event_loop_thread.cpp
    ├── test_event_loop_thread_pool.cpp
    ├── test_timer_queue.cpp
    ├── test_idle_connection.cpp
    └── test_logger.cpp
```

## Build & Run

### Prerequisites

- Linux (kernel 2.6.27+ for `epoll_create1`)
- GCC 7+ or Clang 5+ (C++17 support)
- CMake 3.16+
- Google Test (auto-downloaded by CMake if not found)

### Build

```bash
# Clone
git clone https://github.com/islxhere/cpp-http-server.git
cd cpp-http-server

# Build
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### Run

```bash
# Start the HTTP server (port 8080, 4 worker threads)
./build/http_server

# Test with curl
curl http://localhost:8080/
# <h1>Hello! Handled by Thread: 12345</h1>

curl http://localhost:8080/other
# <h1>404 Not Found</h1>
```

### Run Tests

```bash
cd build
./run_tests
```

Expected output:

```
[==========] Running 69 tests from 14 test suites.
[  PASSED  ] 69 tests.
```

## Module Design

### Network Layer (core/)

| Module | Responsibility |
|--------|---------------|
| **InetAddress** | Wraps `sockaddr_in`, provides IP + port construction and access |
| **Buffer** | Muduo-style read/write buffer with `readv` scatter-read and prepend space |
| **Poller** | Wraps `epoll_create1` / `epoll_ctl` / `epoll_wait`, returns active channels |
| **Channel** | Manages a single fd, binds read/write/close/error callbacks |
| **EventLoop** | Drives the event loop: poll -> handleEvent -> doPendingFunctors |
| **Acceptor** | Listens on port, `accept4()` in a loop, notifies via callback |
| **TcpConnection** | Represents a TCP connection with input/output buffers and `shared_ptr` lifecycle |
| **TcpServer** | Manages Acceptor + all connections, distributes to Sub Reactors |
| **EventLoopThread** | Starts an EventLoop in an independent thread with synchronization |
| **EventLoopThreadPool** | Manages Sub Reactor threads, round-robin connection distribution |

### HTTP Layer (http/)

| Module | Responsibility |
|--------|---------------|
| **HttpRequest** | Stores parsed HTTP request (method, path, query, headers, body) |
| **HttpResponse** | Constructs HTTP response and serializes to Buffer |
| **HttpParser** | Stateless static methods: `parseRequestLine()`, `parseHeader()` |
| **HttpContext** | State machine driving HTTP parsing from Buffer (handles half-packet/sticky-packet) |
| **HttpServer** | Wraps TcpServer, binds HttpContext per connection via `std::any` |

### Utilities (utils/)

| Module | Responsibility |
|--------|---------------|
| **Timer** | Single timer task with callback, expiration, and optional repeat interval |
| **TimerQueue** | Manages all timers using `timerfd`, converts timeout events to epoll-readable events |
| **Logger** | Async singleton logger with double-buffered background thread writing |

## Key Design Decisions

### Why `readv` instead of `read`?

Buffer's `readFd()` uses `readv` scatter-read with a 64KB stack buffer. When the Buffer's writable space is insufficient, data is read into the stack buffer first, then appended. This avoids calling `ensureWritableBytes` before every read.

### Why `shared_ptr` for TcpConnection?

TCP connection lifecycles are complex: connections may be closed during callbacks, or the peer may disconnect while sending data. `shared_ptr` + `enable_shared_from_this` allows safely passing `shared_from_this()` in callbacks, avoiding dangling pointers.

### Why timerfd instead of `std::condition_variable::wait_for`?

timerfd registers directly with epoll, so timeout events and IO events are handled in the same event loop without needing an additional thread.

### Why double-buffered logging?

The frontend writes to `buffer_` under a lock, then `notify_one()`. The backend thread swaps `buffer_` to a local vector (O(1) pointer swap), immediately releases the lock, then writes to file without holding the lock. This minimizes lock contention and ensures logging never blocks the IO threads.

## Test Coverage

| Test Suite | Tests | Coverage |
|-----------|-------|----------|
| InetAddressTest | 6 | IP/port construction, sockaddr conversion |
| BufferTest | 11 | Read/write, readfd, prepend, expand |
| ChannelTest | 10 | Enable/disable events, callback dispatch |
| PollerTest | 7 | epoll lifecycle, multi-channel |
| EventLoopTest | 1 | timerfd integration |
| AcceptorConnectionTest | 1 | End-to-end echo test |
| HttpRequestTest | 6 | Request data model |
| HttpResponseTest | 8 | Response serialization |
| HttpParserTest | 5 | Half-packet, sticky-packet, Keep-Alive |
| EventLoopThreadTest | 4 | Thread synchronization, callback dispatch |
| EventLoopThreadPoolTest | 3 | Round-robin, multi-thread dispatch |
| TimerQueueTest | 2 | One-shot & repeating timers |
| IdleConnectionTest | 2 | Idle timeout kick, active connection keep-alive |
| LoggerTest | 3 | 10000-line multi-thread write, log-level filter |
| **Total** | **69** | |

## Tech Stack

| Option | Choice | Reason |
|--------|--------|--------|
| IO Multiplexing | epoll (ET mode) | Most efficient on Linux, ET reduces syscalls |
| Threading Model | Main-Sub Reactor | Industry standard (Nginx / Netty) |
| C++ Standard | C++17 | `string_view`, `optional`, `any`, structured bindings |
| Build System | CMake 3.16+ | Industry standard |
| Testing | Google Test | Mainstream, easy integration |
| Serialization | None (plain HTTP) | No external dependencies |

## Development Log

Detailed development logs for each phase are available in the `docs/` directory:

- [Phase 1: Single-threaded Reactor](docs/phase1_log.md) — Core network layer (8 modules)
- [Phase 2: HTTP Protocol Layer](docs/phase2_log.md) — HTTP parsing and response (5 modules)
- [Phase 3: Multi-threaded Reactor](docs/phase3_log.md) — Thread pool and connection distribution (3 modules)
- [Phase 4: Stability & Observability](docs/phase4_log.md) — Timer, idle timeout, async logging (6 modules)

## Code Statistics

| Directory | Files | Lines |
|-----------|-------|-------|
| src/core/ | 20 | ~1,600 |
| src/http/ | 10 | ~608 |
| src/utils/ | 6 | ~553 |
| src/main.cpp | 1 | 48 |
| tests/ | 13 | ~1,549 |
| **Total** | **50** | **~4,358** |

## Benchmark

Test environment: Linux 6.8.0 / 4 CPU cores / wrk 4.1.0 / 10s per test

![QPS Benchmark](docs/benchmark.svg)

### QPS Comparison

| Connections | 1 Thread | 4 Threads | Speedup |
|-------------|----------|-----------|---------|
| 100 | 77,761 req/s | 147,166 req/s | **+89%** |
| 500 | 69,520 req/s | 138,166 req/s | **+99%** |
| 1000 | 54,931 req/s | 122,764 req/s | **+123%** |

### Latency Comparison

| Connections | 1 Thread (avg) | 4 Threads (avg) |
|-------------|----------------|-----------------|
| 100 | 1.28 ms | 0.96 ms |
| 500 | 7.15 ms | 3.90 ms |
| 1000 | 18.10 ms | 8.39 ms |

### Throughput Comparison

| Connections | 1 Thread | 4 Threads |
|-------------|----------|-----------|
| 100 | 9.49 MB/s | 17.96 MB/s |
| 500 | 8.49 MB/s | 16.87 MB/s |
| 1000 | 6.71 MB/s | 14.99 MB/s |

> For interactive charts, open [docs/benchmark.html](https://islxhere.github.io/cpp-http-server/docs/benchmark.html) in a browser.

## License

This project is for educational purposes.
