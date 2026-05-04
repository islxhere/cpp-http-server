# 阶段三：多线程主从 Reactor — 开发日志

> 项目：高并发 HTTP/1.1 服务器（C++17 / Linux epoll）
> 日期：2026-05-02
> 状态：全部完成，测试通过

---

## 一、阶段概述

阶段三将阶段一、二的单线程服务器改造为主从 Reactor 多线程架构，共 3 个模块（6 个源文件），总计约 274 行 C++ 代码。改造后服务器可以充分利用多核 CPU，Sub Reactor 线程数可配置。

### 完成的模块

| # | 模块 | 文件 | 行数 | 说明 |
|---|------|------|------|------|
| 1 | EventLoopThread | `event_loop_thread.h/cpp` | 92 | 独立线程运行 EventLoop |
| 2 | EventLoopThreadPool | `event_loop_thread_pool.h/cpp` | 82 | 管理 Sub Reactor 线程池 |
| 3 | TcpServer 改造 | `tcp_server.h/cpp` | 163 | 多线程连接分发 |

### 测试文件

| 测试文件 | 测试数 | 覆盖模块 |
|----------|--------|----------|
| `test_event_loop_thread.cpp` | 4 | EventLoopThread |
| `test_event_loop_thread_pool.cpp` | 3 | EventLoopThreadPool |
| **合计** | **7** | |

---

## 二、架构设计

### 2.1 主从 Reactor 架构

```
┌─────────────────────────────────────────────┐
│                  Main Reactor                │
│   MainLoop(单线程) + Acceptor                │
│   负责监听端口，accept 新连接                  │
└────────────────┬────────────────────────────┘
                 │ 新连接通过 Round-Robin 分发
    ┌────────────┼────────────┐
    ▼            ▼            ▼
┌────────┐  ┌────────┐  ┌────────┐
│SubLoop1│  │SubLoop2│  │SubLoop3│   Sub Reactors
│(线程1) │  │(线程2) │  │(线程3) │   每个线程一个 EventLoop
└────┬───┘  └───┬────┘  └───┬────┘
     │          │            │
  Connection Connection  Connection
  HTTP解析    HTTP解析    HTTP解析
  读/写       读/写       读/写
```

**核心思路**：
- Main Reactor 只做 accept，不处理业务
- Sub Reactor 负责已建立连接的所有 IO 和 HTTP 处理
- 每个 Sub Reactor 跑在独立线程，线程数可配置（默认 0 = 单线程模式）

### 2.2 连接分发流程

```
客户端 connect()
  → Acceptor::handleRead() → accept4() 获取 conn_fd
  → TcpServer::newConnection(sockfd, peer_addr)
      → EventLoopThreadPool::getNextLoop()  // Round-Robin 选取 SubLoop
      → 在 SubLoop 上创建 TcpConnection
      → SubLoop->queueInLoop(conn->connectEstablished)
      // connectEstablished 在 SubLoop 线程中执行，注册 Channel 到 SubLoop 的 Poller
```

---

## 三、各模块详细设计

### 3.1 EventLoopThread — Sub Reactor 线程

**职责**：在独立线程中创建并运行 EventLoop。

**设计要点**：
- `startLoop()` 启动子线程，阻塞等待直到 EventLoop 创建完毕
- 使用 `mutex_` + `cond_` 同步：子线程创建 EventLoop 后 notify，主线程 wait 返回
- 返回 EventLoop 指针供上层使用
- 析构时设置 `exiting_` 标志，调用 `loop_->quit()`，join 线程

**同步机制**：
```cpp
EventLoop* EventLoopThread::startLoop() {
    thread_ = std::thread([this]() { threadFunc(); });
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]() { return loop_ != nullptr; });
    }
    return loop_;
}

void EventLoopThread::threadFunc() {
    EventLoop loop;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = &loop;
    }
    cond_.notify_one();
    loop.loop();
    // loop 退出后
    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = nullptr;
}
```

### 3.2 EventLoopThreadPool — 线程池管理

**职责**：管理多个 Sub Reactor 线程，通过 Round-Robin 策略分发新连接。

**设计要点**：
- `setThreadNum(n)` 设置 Sub Reactor 线程数
- `start()` 创建 n 个 EventLoopThread 并启动
- `getNextLoop()` Round-Robin 轮询，返回下一个 SubLoop
- 如果 `num_threads_ == 0`，`getNextLoop()` 返回 `base_loop_`（单线程回退）

**Round-Robin 实现**：
```cpp
EventLoop* EventLoopThreadPool::getNextLoop() {
    EventLoop* loop = base_loop_;
    if (!loops_.empty()) {
        loop = loops_[next_];
        ++next_;
        if (static_cast<size_t>(next_) >= loops_.size()) {
            next_ = 0;
        }
    }
    return loop;
}
```

### 3.3 TcpServer 多线程改造

**职责**：改造为支持多线程的主从 Reactor 架构。

**改造要点**：
- 新增 `EventLoopThreadPool` 成员
- `setThreadNum()` 透传到线程池
- `newConnection()` 通过线程池获取 SubLoop，在 SubLoop 上创建连接
- `removeConnection()` 通过 `queueInLoop` 确保在正确线程中执行清理

**新连接建立流程**：
```cpp
void TcpServer::newConnection(int sockfd, const InetAddress& peer_addr) {
    // 从线程池获取一个 SubLoop
    EventLoop* io_loop = thread_pool_->getNextLoop();

    auto conn = std::make_shared<TcpConnection>(io_loop, sockfd, local_addr, peer_addr);
    connections_[name] = conn;

    conn->setMessageCallback(message_callback_);
    conn->setCloseCallback([this](auto& c) { removeConnection(c); });

    // 通知上层新连接建立（如 HttpServer 绑定 HttpContext）
    if (connection_callback_) {
        connection_callback_(conn);
    }

    // 在 SubLoop 线程中完成连接建立
    io_loop->queueInLoop([conn]() { conn->connectEstablished(); });
}
```

**连接清理流程**：
```cpp
void TcpServer::removeConnection(const TcpConnectionPtr& conn) {
    EventLoop* io_loop = conn->getLoop();

    // 从 MainLoop 的 connections_ 字典中移除
    loop_->queueInLoop([this, conn]() {
        connections_.erase(conn->name());
    });

    // 在 SubLoop 线程中销毁连接
    io_loop->queueInLoop([conn]() { conn->connectDestroyed(); });
}
```

---

## 四、关键技术决策

### 4.1 为什么需要 eventfd 唤醒机制？

当 SubLoop 线程在 `epoll_wait` 中阻塞时，如果有新的回调需要执行（如 `connectEstablished`），需要一种方式唤醒它。`eventfd` 是 Linux 提供的轻量级事件通知机制，写入一个 uint64 即可触发可读事件，比 pipe 更高效。

**唤醒流程**：
```
MainLoop 调用 SubLoop->queueInLoop(cb)
  → 加锁，push cb 到 pending_functors_
  → write(eventfd_, 1)  // 唤醒 SubLoop
  → SubLoop 的 epoll_wait 返回
  → handleRead() 读掉 eventfd 中的数据
  → doPendingFunctors() 执行 cb
```

### 4.2 为什么 connectEstablished 要在 SubLoop 线程中执行？

`connectEstablished()` 调用 `channel_->enableReading()`，这会触发 `epoll_ctl(EPOLL_CTL_ADD)`。epoll_ctl 必须在 epoll 所属线程中调用（或者说，在拥有该 EventLoop 的线程中调用）。通过 `queueInLoop` 将 `connectEstablished` 投递到 SubLoop 线程，确保线程安全。

### 4.3 为什么 removeConnection 要双重 queueInLoop？

连接清理涉及两个线程：
1. **MainLoop 线程**：持有 `connections_` 字典，erase 操作必须在 MainLoop 中
2. **SubLoop 线程**：持有 Channel 和 fd，`connectDestroyed()` 必须在 SubLoop 中

因此 `removeConnection` 分别向两个线程投递清理任务。

### 4.4 为什么 num_threads=0 时回退到 base_loop？

这种设计保持了向后兼容：
- 开发阶段可以设置 `setThreadNum(0)` 使用单线程模式，便于调试
- 生产环境设置 `setThreadNum(4)` 启用多线程
- 代码路径完全一致，只是 `getNextLoop()` 返回 base_loop

---

## 五、测试验证

### 5.1 单元测试（7 个）

```
[==========] Running 7 tests from 2 test suites.
[  PASSED  ] EventLoopThreadTest (4 tests)
    - StartLoopReturnsNonNull
    - QueueInLoopExecutesInSubThread
    - MultipleTasksExecuted
    - LoopRunsInDifferentThread
[  PASSED  ] EventLoopThreadPoolTest (3 tests)
    - SingleThreadFallback
    - RoundRobin
    - SubLoopsCanDispatch
[==========] 7 tests passed.
```

### 5.2 关键测试用例

**SubLoop 线程执行回调**：
```
创建 EventLoopThread
startLoop() 获取 SubLoop
SubLoop->queueInLoop(task)
验证 task 在 SubLoop 线程中执行（tid 不同）
验证 task 执行完成（atomic counter）
```

**Round-Robin 分发**：
```
创建 EventLoopThreadPool，设置 3 个线程
连续调用 6 次 getNextLoop()
验证轮询顺序：loop0 → loop1 → loop2 → loop0 → loop1 → loop2
```

### 5.3 多线程 HTTP Server 验证

```bash
# 启动 4 线程 HTTP Server
./http_server

# 浏览器访问 http://localhost:8080
# 多次刷新，观察 Thread ID 变化：
# <h1>Hello! Handled by Thread: 12345</h1>
# <h1>Hello! Handled by Thread: 12346</h1>
# <h1>Hello! Handled by Thread: 12347</h1>
# <h1>Hello! Handled by Thread: 12348</h1>
```

---

## 六、与前序阶段的衔接

### 对阶段一的改造

- **TcpServer**：新增 `EventLoopThreadPool` 成员，`newConnection()` 改为从线程池获取 SubLoop
- **EventLoop**：新增 `eventfd_` 唤醒机制和 `queueInLoop()` 方法（阶段一已有，此处正式使用）
- 其他模块（Poller、Channel、Buffer 等）零修改

### 对阶段二的兼容

- HttpServer 完全不需要修改，它只使用 TcpServer 的回调接口
- HttpContext 通过 `std::any` 绑定在 TcpConnection 上，线程安全由 TcpConnection 的所属 EventLoop 保证

---

## 七、文件清单

```
src/core/
├── event_loop_thread.h       (39 行)
├── event_loop_thread.cpp     (53 行)
├── event_loop_thread_pool.h  (39 行)
├── event_loop_thread_pool.cpp(43 行)
├── tcp_server.h              (58 行) — 改造
└── tcp_server.cpp            (105 行) — 改造

tests/
├── test_event_loop_thread.cpp      (65 行)
└── test_event_loop_thread_pool.cpp (77 行)

总计：约 274 行 C++ 代码 + 142 行测试
```

---

## 八、后续计划

阶段三完成后，项目将进入阶段四（稳定性）：
- Timer + TimerQueue — 定时器，用于清理超时连接
- Logger — 异步日志系统
- 性能调优 + 压测报告
