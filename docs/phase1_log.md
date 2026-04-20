# 阶段一：单线程 Reactor — 开发日志

> 项目：高并发 HTTP/1.1 服务器（C++17 / Linux epoll）
> 日期：2026-05-02
> 状态：全部完成，测试通过

---

## 一、阶段概述

阶段一实现了单线程 Reactor 模式的核心网络层，共 8 个模块（16 个源文件），总计约 1900 行 C++ 代码。

### 完成的模块

| # | 模块 | 文件 | 行数 | 说明 |
|---|------|------|------|------|
| 1 | InetAddress | `inet_address.h/cpp` | 78 | 封装 sockaddr_in |
| 2 | Buffer | `buffer.h/cpp` | 186 | 应用层读写缓冲区（muduo 风格） |
| 3 | Poller | `poller.h/cpp` | 126 | epoll 封装 |
| 4 | Channel | `channel.h/cpp` | 196 | fd 事件分发 |
| 5 | EventLoop | `event_loop.h/cpp` | 158 | Reactor 核心事件循环 |
| 6 | Acceptor | `acceptor.h/cpp` | 117 | 监听端口、接受新连接 |
| 7 | TcpConnection | `tcp_connection.h/cpp` | 200 | TCP 连接管理 |
| 8 | TcpServer | `tcp_server.h/cpp` | 114 | 服务端入口 |

### 测试文件

| 测试文件 | 测试数 | 覆盖模块 |
|----------|--------|----------|
| `test_inet_address.cpp` | 6 | InetAddress |
| `test_buffer.cpp` | 11 | Buffer |
| `test_poller.cpp` | 7 | Poller |
| `test_channel.cpp` | 10 | Channel |
| `test_event_loop.cpp` | 1 | EventLoop（timerfd 集成测试） |
| `test_acceptor_connection.cpp` | 1 | Acceptor + TcpConnection（echo 集成测试） |
| **合计** | **36** | |

### Echo Server

`src/main.cpp` 实现了完整的 echo 服务器，监听 8080 端口，可通过 `telnet localhost 8080` 验证。

---

## 二、架构设计

### 2.1 整体架构图

```
┌─────────────────────────────────────────────┐
│                 TcpServer                    │
│  持有 Acceptor + connections_ 字典           │
│  注册 ConnectionCallback / MessageCallback   │
└──────────┬──────────────────────────────────┘
           │ newConnection()
           ▼
┌─────────────────────────────────────────────┐
│                  Acceptor                    │
│  listen_fd_ + Channel                        │
│  handleRead() → accept4() 循环              │
└──────────┬──────────────────────────────────┘
           │ NewConnectionCallback(sockfd, peer_addr)
           ▼
┌─────────────────────────────────────────────┐
│               TcpConnection                  │
│  conn_fd_ + Channel + input/output Buffer    │
│  handleRead() → readFd → MessageCallback     │
│  handleWrite() → write → disableWriting      │
│  handleClose() → CloseCallback               │
└──────────┬──────────────────────────────────┘
           │ 生命周期由 shared_ptr 管理
           ▼
┌─────────────────────────────────────────────┐
│                 EventLoop                    │
│  loop(): poll → handleEvent → doPendingFunctors │
│  queueInLoop(): 延后执行回调                 │
│  持有 Poller，每线程最多一个                 │
└──────────┬──────────────────────────────────┘
           │
           ▼
┌─────────────────────────────────────────────┐
│                  Poller                      │
│  epoll_create1 / epoll_ctl / epoll_wait      │
│  poll() 填充 active_channels                 │
└─────────────────────────────────────────────┘
```

### 2.2 数据流

```
客户端 connect()
  → Acceptor::handleRead() → accept4() 获取 conn_fd
  → TcpServer::newConnection() 创建 TcpConnection
  → TcpConnection::connectEstablished() → enableReading()

客户端发送数据
  → EventLoop::loop() → Poller::poll() 检测到 EPOLLIN
  → Channel::handleEvent() → read_callback_
  → TcpConnection::handleRead() → input_buffer_.readFd()
  → MessageCallback(conn, buf)
  → conn->send(data) → output_buffer_ 或直接 write()

客户端断开
  → handleRead() 返回 0 → handleClose()
  → CloseCallback → TcpServer::removeConnection()
  → queueInLoop → destroyConnection()
  → connectDestroyed() → disableAll + remove + close
```

---

## 三、各模块详细设计

### 3.1 InetAddress — 网络地址封装

**职责**：封装 `sockaddr_in`，提供 IP + port 的构造和访问接口。

**设计要点**：
- 支持三种构造方式：ip+port、sockaddr_in、默认构造
- 使用 `inet_pton` / `inet_ntop` 进行地址转换
- `ipPort()` 返回 `"ip:port"` 格式字符串，用于 TcpConnection 命名

**接口**：
```cpp
class InetAddress {
public:
    explicit InetAddress(uint16_t port, const std::string& ip = "0.0.0.0");
    explicit InetAddress(const struct sockaddr_in& addr);
    InetAddress();

    std::string ip() const;
    uint16_t port() const;
    std::string ipPort() const;

    const struct sockaddr_in& getSockAddr() const;
    void setSockAddr(const struct sockaddr_in& addr);
};
```

### 3.2 Buffer — 应用层缓冲区

**职责**：提供高效的读写缓冲区，参考 muduo 的 Buffer 设计。

**内存布局**：
```
+-------------------+------------------+------------------+
| prependable bytes |  readable bytes  |  writable bytes  |
|                   |     (CONTENT)    |                  |
+-------------------+------------------+------------------+
0      <=      readerIndex   <=   writerIndex    <=     size()
```

**设计要点**：
- 内部使用 `vector<char>`，初始大小 1024 字节
- 预留 8 字节 prepend 空间，用于协议头插入
- `readFd()` 使用 `readv` 分散读，栈上 64KB 额外缓冲区避免频繁扩容
- `ensureWritableBytes()` 优先内部搬移数据，空间不足时才 resize

**关键实现**：
```cpp
ssize_t Buffer::readFd(int fd, int* saved_errno) {
    char extrabuf[65536];
    struct iovec vec[2];
    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writableBytes();
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);
    const ssize_t n = ::readv(fd, vec, 2);
    // ... 处理读取结果
}
```

### 3.3 Poller — epoll 封装

**职责**：封装 epoll 系统调用，监听 fd 上的 IO 事件。

**设计要点**：
- `epoll_create1(EPOLL_CLOEXEC)` 创建 epoll fd
- `poll()` 调用 `epoll_wait`，将活跃事件填入 `active_channels`
- `updateChannel()` 根据 Channel 是否有关注事件决定 ADD/DEL/MOD
- `events_` 初始大小 16，活跃事件满时自动扩容（doubling）

**接口**：
```cpp
class Poller {
public:
    void poll(int timeout_ms, ChannelList* active_channels);
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
};
```

### 3.4 Channel — 事件分发器

**职责**：管理单个 fd 的事件注册和回调分发。不拥有 fd。

**设计要点**：
- 持有 `events_`（关注事件）和 `revents_`（实际事件）
- `handleEvent()` 根据 revents 依次检查 EPOLLHUP/EPOLLERR/EPOLLIN/EPOLLOUT
- 通过 `update()` / `remove()` 委托给 EventLoop 操作 Poller
- 支持四种回调：read / write / close / error

**事件处理顺序**：
```cpp
void Channel::handleEvent() {
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))  → close_callback_
    if (revents_ & EPOLLERR)                              → error_callback_
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))    → read_callback_
    if (revents_ & EPOLLOUT)                              → write_callback_
}
```

### 3.5 EventLoop — Reactor 核心

**职责**：驱动事件循环，分发 IO 事件，管理待执行回调。

**设计要点**：
- 每线程最多一个 EventLoop（`thread_local` 检查）
- `loop()` 核心循环：poll → handleEvent → doPendingFunctors
- `queueInLoop()` 支持延后执行回调（mutex 保护的队列）
- `doPendingFunctors()` 每轮循环末尾批量执行，swap 减少锁持有时间

**事件循环**：
```cpp
void EventLoop::loop() {
    while (!quit_) {
        active_channels_.clear();
        poller_->poll(10000, &active_channels_);
        for (Channel* channel : active_channels_) {
            channel->handleEvent();
        }
        doPendingFunctors();
    }
}
```

### 3.6 Acceptor — 连接接受器

**职责**：监听端口，接受新连接，通过回调通知上层。

**设计要点**：
- 构造时创建非阻塞 listen fd（`SOCK_NONBLOCK | SOCK_CLOEXEC`）
- `SO_REUSEADDR` 避免 TIME_WAIT 端口占用
- `handleRead()` 循环 `accept4()` 直到 EAGAIN
- 通过 `NewConnectionCallback` 传递 sockfd 和 peer 地址

**生命周期**：
```
构造 → socket + bind
listen() → ::listen + enableReading
handleRead() → accept4() 循环 → NewConnectionCallback
析构 → disableAll + remove + close
```

### 3.7 TcpConnection — TCP 连接

**职责**：代表一条已建立的 TCP 连接，管理读写和生命周期。

**设计要点**：
- 继承 `enable_shared_from_this`，生命周期由 `shared_ptr` 管理
- 持有 `unique_ptr<Channel>` + 输入输出两个 Buffer
- `name_` 存储 peer 的 `"ip:port"`，用作连接标识
- `send()` 优先直接 write，写不完则追加到 output_buffer 并使能写事件
- `handleClose()` 通过 CloseCallback 通知上层，由 TcpServer 延后销毁

**数据读取流程**：
```
handleRead()
  → input_buffer_.readFd(conn_fd_)
  → if (n > 0)  message_callback_(shared_from_this(), &input_buffer_)
  → if (n == 0) handleClose()  // 对端关闭
  → if (n < 0)  handleError()
```

**数据发送流程**：
```
send(message)
  → if output_buffer 为空，先尝试 write()
  → if 全部写完，触发 write_complete_callback_
  → if 有剩余，append 到 output_buffer，enableWriting()
```

### 3.8 TcpServer — 服务端入口

**职责**：管理 Acceptor 和所有 TcpConnection 的生命周期。

**设计要点**：
- 持有 `unique_ptr<Acceptor>` 和 `unordered_map<string, TcpConnectionPtr>`
- 连接名使用 peer 的 `"ip:port"` 格式
- `removeConnection()` 通过 `queueInLoop` 延后销毁，避免回调执行到一半对象被 erase
- 用户只需设置 ConnectionCallback 和 MessageCallback

**连接生命周期管理**：
```
newConnection()
  → make_shared<TcpConnection>
  → connections_[name] = conn
  → 设置 MessageCallback / CloseCallback
  → connectEstablished()

removeConnection()
  → queueInLoop → destroyConnection()
  → connections_.erase(name)
  → connectDestroyed()
```

---

## 四、关键技术决策

### 4.1 为什么用 readv 而不是 read？

Buffer 的 `readFd()` 使用 `readv` 分散读，配合栈上的 64KB 额外缓冲区。这样当 Buffer 可写空间不足时，数据先读到栈上缓冲区，再 append 到 Buffer。避免了每次 read 前都要 ensureWritableBytes 的开销。

### 4.2 为什么 TcpConnection 用 shared_ptr？

TCP 连接的生命周期复杂：可能在回调中被关闭，也可能在发送数据时对端断开。使用 `shared_ptr` + `enable_shared_from_this` 可以安全地在回调中传递 `shared_from_this()`，避免悬垂指针。

### 4.3 为什么 removeConnection 要延后？

当 `handleClose()` 触发 CloseCallback 时，我们还在 Channel 的回调栈中。如果此时直接 erase 连接，会导致 Channel 对象被析构，后续的回调分发可能访问已释放的内存。通过 `queueInLoop` 延后到本轮事件处理结束后再销毁，确保安全。

### 4.4 为什么 Channel 不拥有 fd？

Channel 的职责是事件分发，不是资源管理。fd 的生命周期由 Acceptor（listen fd）或 TcpConnection（conn fd）管理。这样 Channel 可以被安全地创建和销毁，而不影响底层 fd。

---

## 五、测试验证

### 5.1 单元测试（36 个）

```
[==========] Running 36 tests from 6 test suites.
[  PASSED  ] InetAddressTest (6 tests)
[  PASSED  ] BufferTest (11 tests)
[  PASSED  ] PollerTest (7 tests)
[  PASSED  ] ChannelTest (10 tests)
[  PASSED  ] EventLoopTest (1 test)
[  PASSED  ] AcceptorConnectionTest (1 test)
[==========] 36 tests passed.
```

### 5.2 Echo Server 功能验证

```bash
# 终端 1：启动服务器
./http_server
# Echo Server starting on port 8080...
# Echo Server is running on port 8080
# New connection: 127.0.0.1:xxxxx
# Received 12 bytes from 127.0.0.1:xxxxx

# 终端 2：telnet 连接
$ telnet localhost 8080
hello world
hello world
```

---

## 六、文件清单

```
src/core/
├── inet_address.h      (35 行)
├── inet_address.cpp     (43 行)
├── buffer.h             (71 行)
├── buffer.cpp           (115 行)
├── poller.h             (43 行)
├── poller.cpp           (83 行)
├── channel.h            (74 行)
├── channel.cpp          (122 行)
├── event_loop.h         (65 行)
├── event_loop.cpp       (93 行)
├── acceptor.h           (39 行)
├── acceptor.cpp         (78 行)
├── tcp_connection.h     (68 行)
├── tcp_connection.cpp   (132 行)
├── tcp_server.h         (51 行)
└── tcp_server.cpp       (63 行)

src/
└── main.cpp             (35 行) — Echo Server

tests/
├── test_inet_address.cpp (64 行)
├── test_buffer.cpp       (154 行)
├── test_poller.cpp       (157 行)
├── test_channel.cpp      (141 行)
├── test_event_loop.cpp   (62 行)
└── test_acceptor_connection.cpp (127 行)

总计：约 1915 行 C++ 代码
```

---

## 七、后续计划

阶段一完成后，项目将进入阶段二（HTTP 协议层）：
- `HttpRequest` — HTTP 请求数据结构
- `HttpResponse` — HTTP 响应构造
- `HttpParser` — 状态机解析 HTTP 报文
- `HttpContext` — 绑定在 TcpConnection 上的 HTTP 状态
- `HttpServer` — 在 TcpServer 基础上封装 HTTP 语义
