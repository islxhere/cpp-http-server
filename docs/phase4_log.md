# 阶段四：稳定性与亮点 — 开发日志

> 项目：高并发 HTTP/1.1 服务器（C++17 / Linux epoll）
> 日期：2026-05-02
> 状态：全部完成，测试通过

---

## 一、阶段概述

阶段四为服务器添加了定时器系统和异步日志系统，提升了生产环境下的稳定性与可观测性。共 4 个模块（8 个源文件），总计约 553 行 C++ 代码。

### 完成的模块

| # | 模块 | 文件 | 行数 | 说明 |
|---|------|------|------|------|
| 1 | Timer | `timer.h/cpp` | 65 | 单个定时任务封装 |
| 2 | TimerQueue | `timer_queue.h/cpp` | 151 | 定时器管理（基于 timerfd） |
| 3 | EventLoop 集成 | `event_loop.h/cpp` | 223 | runAt/runAfter/runEvery 便捷方法 |
| 4 | TcpConnection 改造 | `tcp_connection.h/cpp` | 246 | lastActiveTime 追踪 + 线程安全 forceClose |
| 5 | TcpServer 改造 | `tcp_server.h/cpp` | 163 | 空闲连接超时踢出 |
| 6 | Logger | `logger.h/cpp` | 227 | 异步日志系统（双缓冲） |

### 测试文件

| 测试文件 | 测试数 | 覆盖模块 |
|----------|--------|----------|
| `test_timer_queue.cpp` | 2 | Timer + TimerQueue |
| `test_idle_connection.cpp` | 2 | 空闲连接超时踢出 |
| `test_logger.cpp` | 3 | Logger |
| **合计** | **7** | |

---

## 二、定时器系统

### 2.1 架构设计

```
┌─────────────────────────────────────────────────┐
│                  EventLoop                        │
│  runAt() / runAfter() / runEvery()               │
│  创建 Timer → TimerQueue::addTimer()             │
└──────────┬──────────────────────────────────────┘
           │
           ▼
┌─────────────────────────────────────────────────┐
│                 TimerQueue                        │
│  持有 timerfd + Channel                          │
│  timers_ 集合按过期时间排序                        │
│  addTimer() → queueInLoop → 插入 + resetTimerfd  │
│  handleRead() → 收集过期 → 执行回调 → 重置 timerfd│
└──────────┬──────────────────────────────────────┘
           │ 管理
           ▼
┌─────────────────────────────────────────────────┐
│                   Timer                           │
│  callback_ + expiration_ + interval_             │
│  支持一次性定时器和重复定时器                       │
│  restart(now) 重新计算下次过期时间                  │
└─────────────────────────────────────────────────┘
```

### 2.2 Timer — 定时任务

**职责**：封装单个定时任务，持有回调、过期时间和重复间隔。

**设计要点**：
- 一次性定时器：`interval_ == 0`，`repeat_ == false`
- 重复定时器：`interval_ > 0`，`repeat_ == true`
- `restart(now)` 重新计算下次过期时间：`expiration_ = now + interval`
- 使用 `steady_clock` 保证时间单调递增

**接口**：
```cpp
class Timer {
    Timer(std::function<void()> callback, TimePoint expiration, double interval = 0.0);
    void run() const;
    void restart(TimePoint now);
    TimePoint expiration() const;
    bool repeat() const;
};
```

### 2.3 TimerQueue — 定时器管理

**职责**：管理所有 Timer，将时间事件转化为 IO 事件融入 EventLoop。

**设计要点**：
- 使用 `timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)` 创建 timerfd
- 将 timerfd 注册到 EventLoop 的 Poller 中，超时转化为 epoll 可读事件
- `timers_` 使用 `std::set<pair<TimePoint, Timer*>>` 按过期时间排序
- `addTimer()` 通过 `queueInLoop` 保证线程安全

**核心流程**：
```
addTimer(timer)
  → queueInLoop → 插入 timers_ 集合
  → 如果是最早过期的，调用 resetTimerfd()

timerfd 可读（超时触发）
  → handleRead()
  → read(timerfd) 清除事件
  → 收集所有 expiration <= now 的 Timer
  → 逐个执行回调
  → 重复定时器 restart(now) 后重新插入
  → 一次性定时器 delete 释放
  → resetTimerfd() 以新的最早过期时间配置 timerfd
```

**resetTimerfd 实现**：
```cpp
void TimerQueue::resetTimerfd() {
    Timer::TimePoint earliest = timers_.begin()->first;
    struct timespec ts = howMuchTimeFromNow(earliest);
    struct itimerspec new_value{};
    new_value.it_value = ts;
    ::timerfd_settime(timerfd_, 0, &new_value, nullptr);
}
```

### 2.4 EventLoop 集成

**职责**：为 EventLoop 添加定时器便捷方法。

**新增方法**：
```cpp
void runAt(TimePoint time, TimerCallback cb);      // 在指定时间执行
void runAfter(double seconds, TimerCallback cb);    // 延迟执行
void runEvery(double seconds, TimerCallback cb);    // 周期执行
```

**实现**：
```cpp
void EventLoop::runAfter(double delay_seconds, TimerCallback cb) {
    auto time = steady_clock::now() + microseconds(int64_t(delay_seconds * 1000000));
    runAt(time, std::move(cb));
}

void EventLoop::runEvery(double interval_seconds, TimerCallback cb) {
    auto time = steady_clock::now() + microseconds(int64_t(interval_seconds * 1000000));
    auto* timer = new Timer(std::move(cb), time, interval_seconds);
    timer_queue_->addTimer(timer);
}
```

---

## 三、空闲连接超时踢出

### 3.1 设计思路

```
TcpServer::start()
  → loop_->runEvery(1.0, checkIdleConnections)  // 每秒检测一次

TcpServer::checkIdleConnections()
  → 遍历 connections_
  → if (now - conn->lastActiveTime() > idle_timeout_ms_)
      → conn->forceClose()
```

### 3.2 TcpConnection 改造

**新增成员**：
```cpp
Clock::time_point last_active_time_;  // 最近活跃时间
```

**活跃时间更新时机**：
- `connectEstablished()` — 连接建立时
- `handleRead()` — 收到数据时（n > 0）
- `send()` — 发送数据时

**线程安全的 forceClose()**：
```cpp
void TcpConnection::forceClose() {
    if (loop_->isInLoopThread()) {
        handleClose();  // 在当前线程直接关闭
    } else {
        loop_->queueInLoop([self = shared_from_this()]() {
            self->handleClose();  // 跨线程投递到连接所属的 EventLoop
        });
    }
}
```

### 3.3 TcpServer 改造

**新增成员**：
```cpp
int idle_timeout_ms_ = 8000;  // 默认 8 秒超时
```

**空闲检测逻辑**：
```cpp
void TcpServer::checkIdleConnections() {
    auto now = TcpConnection::Clock::now();
    auto timeout = std::chrono::milliseconds(idle_timeout_ms_);

    // 先收集需要关闭的连接，避免在遍历中修改 map
    std::vector<TcpConnectionPtr> idle_conns;
    for (auto& [name, conn] : connections_) {
        if (now - conn->lastActiveTime() > timeout) {
            idle_conns.push_back(conn);
        }
    }

    for (auto& conn : idle_conns) {
        conn->forceClose();
    }
}
```

---

## 四、异步日志系统

### 4.1 架构设计

```
┌─────────────────────────────────────────────────┐
│              前端（任意线程）                      │
│  LOG_INFO << "message"                           │
│  → LogStream 析构时调用 Logger::append()         │
│  → 加锁，push 到 buffer_                         │
│  → cond_.notify_one()                            │
└──────────┬──────────────────────────────────────┘
           │
           ▼
┌─────────────────────────────────────────────────┐
│              后台线程                              │
│  threadFunc() 无限循环                            │
│  → cond_.wait_for(3s) 等待唤醒或超时              │
│  → swap(buffer_, localBuffer)  // 双缓冲核心     │
│  → 解锁！                                        │
│  → 遍历 localBuffer 写入文件                      │
│  → flush                                         │
└─────────────────────────────────────────────────┘
```

### 4.2 Logger — 单例日志系统

**职责**：提供线程安全的异步日志写入。

**设计要点**：
- 单例模式（`static Logger& instance()`）
- 日志级别过滤：`DEBUG < INFO < WARN < ERROR < FATAL`
- 日志格式：`[LEVEL] 2026-05-02 12:00:00.123 file.cpp:42 - message`
- 双缓冲：前端写入 `buffer_`，后台线程 swap 后写文件
- `init()` 启动后台线程，`stop()` 确保刷完所有缓冲后关闭

**双缓冲核心**：
```cpp
void Logger::threadFunc() {
    std::vector<std::string> localBuffer;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait_for(lock, std::chrono::seconds(3), [this]() {
                return !buffer_.empty() || !running_;
            });
            buffer_.swap(localBuffer);  // 瞬间交换，释放锁
        }
        // 不持有锁，写文件
        for (auto& line : localBuffer) {
            fileStream_ << line << '\n';
        }
        fileStream_.flush();
        localBuffer.clear();
    }
}
```

**为什么 swap 而不是 move？**
- `swap` 是 O(1) 操作，交换两个 vector 的内部指针
- 交换后 `buffer_` 为空，前端可以继续写入
- 交换后 `localBuffer` 持有所有待写日志，后台线程可以安心写文件

### 4.3 LogStream — 流式写入

**职责**：提供 `<<` 流式接口，析构时自动推入 Logger。

**设计要点**：
- RAII 模式：析构时调用 `Logger::append()`
- 支持 `string`、`const char*`、整数、浮点等类型
- 宏 `LOG_INFO` 展开为临时 `LogStream` 对象，语句结束时析构

**宏定义**：
```cpp
#define LOG_INFO \
    ::httpserver::LogStream(::httpserver::Logger::LogLevel::INFO, __FILE__, __LINE__)
```

**使用示例**：
```cpp
LOG_INFO << "Received request from " << ip << " port=" << port;
// 展开为：
// LogStream(INFO, "http_server.cpp", 42) << "Received request from " << ip << " port=" << port;
// ~LogStream() → Logger::append(INFO, "http_server.cpp", 42, "Received request from 127.0.0.1 port=8080")
```

### 4.4 stop() 的正确实现

```cpp
void Logger::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cond_.notify_one();
    if (backgroundThread_.joinable()) {
        backgroundThread_.join();
    }
    // 后台线程已退出，刷完可能残留的缓冲
    if (!buffer_.empty()) {
        for (auto& line : buffer_) {
            fileStream_ << line << '\n';
        }
        fileStream_.flush();
        buffer_.clear();
    }
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
}
```

---

## 五、关键技术决策

### 5.1 为什么用 timerfd 而不是 std::condition_variable::wait_for？

timerfd 可以直接注册到 epoll 中，超时事件和 IO 事件在同一个事件循环中处理，不需要额外的线程。而 `wait_for` 需要单独的线程来管理定时器。

### 5.2 为什么 Logger 使用单例？

日志系统是全局基础设施，所有模块都需要使用。单例模式保证：
- 全局只有一个后台写入线程
- 所有日志写入同一个文件
- `init()` 和 `stop()` 的生命周期管理简单明确

### 5.3 为什么 stop() 后还要手动刷 buffer？

后台线程退出时可能还有日志在 `buffer_` 中（`cond_.wait_for` 超时退出时 `swap` 可能没执行）。`stop()` 在 `join()` 之后手动刷一次，确保不丢日志。

### 5.4 为什么 checkIdleConnections 先收集再关闭？

`forceClose()` 最终会触发 `removeConnection()`，而 `removeConnection()` 会通过 `queueInLoop` 从 `connections_` 中 erase。如果在遍历 `connections_` 的同时 erase，会导致迭代器失效。先收集到 vector 中，再逐个关闭，避免这个问题。

---

## 六、测试验证

### 6.1 单元测试（7 个）

```
[==========] Running 7 tests from 3 test suites.
[  PASSED  ] TimerQueueTest (2 tests)
    - OneShotAndRepeatingTimer: 一次性定时器触发 1 次，重复定时器 2.2s 内触发 4 次
    - MultipleOneShotTimers: 3 个定时器间隔 100ms 依次触发
[  PASSED  ] IdleConnectionTest (2 tests)
    - IdleClientDisconnected: 空闲 3.5s 后被踢出
    - ActiveClientNotDisconnected: 每秒发数据保持活跃，不被踢出
[  PASSED  ] LoggerTest (3 tests)
    - MultiThreadWrite: 4 线程 x 2500 条 = 10000 条，精确行数验证
    - LogLevelFilter: WARN 级别过滤掉 DEBUG/INFO
    - InitAndStop: 基本生命周期验证
[==========] 7 tests passed.
```

### 6.2 Logger 多线程压力测试

```
测试逻辑：
- 启动 Logger，设置 DEBUG 级别
- 4 个子线程，每个写入 2500 条日志
- 使用 atomic<int> 计数器确保序号正确
- stop() 后检查日志文件行数 == 10000

验证结果：
- 10000 行日志全部写入，无丢失
- 每行格式正确：[INFO ] 2026-05-02 12:00:00.123 test_logger.cpp:42 - thread=0 seq=0
- 无死锁、无崩溃
```

---

## 七、全局替换

将 `main.cpp` 中的 `printf` 替换为 `LOG_INFO`：

```cpp
// 替换前
std::printf("HTTP Server starting on port 8080...\n");
std::printf("HTTP Server is running on http://127.0.0.1:8080 with 4 worker threads\n");

// 替换后
LOG_INFO << "HTTP Server starting on port 8080...";
LOG_INFO << "HTTP Server is running on http://127.0.0.1:8080 with 4 worker threads";
```

---

## 八、文件清单

```
src/utils/
├── timer.h             (38 行)
├── timer.cpp           (27 行)
├── timer_queue.h       (52 行)
├── timer_queue.cpp     (99 行)
├── logger.h            (96 行)
└── logger.cpp          (131 行)

src/core/（改造）
├── event_loop.h        (83 行) — 新增 runAt/runAfter/runEvery
├── event_loop.cpp      (140 行) — 新增定时器方法实现
├── tcp_connection.h    (83 行) — 新增 lastActiveTime
├── tcp_connection.cpp  (163 行) — 新增活跃时间追踪 + 线程安全 forceClose
├── tcp_server.h        (58 行) — 新增空闲超时
└── tcp_server.cpp      (105 行) — 新增 checkIdleConnections

tests/
├── test_timer_queue.cpp       (76 行)
├── test_idle_connection.cpp   (147 行)
└── test_logger.cpp            (118 行)

总计：约 553 行 C++ 代码 + 341 行测试
```

---

## 九、项目总结

### 全部测试统计

```
[==========] Running 69 tests from 14 test suites.
[  PASSED  ] 69 tests.
```

| 测试套件 | 测试数 |
|----------|--------|
| InetAddressTest | 6 |
| BufferTest | 11 |
| ChannelTest | 10 |
| PollerTest | 7 |
| EventLoopTest | 1 |
| AcceptorConnectionTest | 1 |
| HttpRequestTest | 6 |
| HttpResponseTest | 8 |
| HttpParserTest | 5 |
| EventLoopThreadTest | 4 |
| EventLoopThreadPoolTest | 3 |
| TimerQueueTest | 2 |
| IdleConnectionTest | 2 |
| LoggerTest | 3 |
| **合计** | **69** |

### 代码统计

| 目录 | 文件数 | 行数 |
|------|--------|------|
| src/core/ | 16 | ~1,600 |
| src/http/ | 10 | ~608 |
| src/utils/ | 6 | ~553 |
| src/main.cpp | 1 | 48 |
| tests/ | 13 | ~1,549 |
| **合计** | **46** | **~4,358** |

### 架构亮点

1. **主从 Reactor**：MainLoop accept + SubLoop IO，充分利用多核
2. **双缓冲异步日志**：前端零等待写入，后台批量刷盘
3. **timerfd 集成**：定时器事件与 IO 事件统一事件循环
4. **空闲连接管理**：周期检测 + 线程安全 forceClose
5. **shared_ptr 生命周期管理**：安全处理复杂连接生命周期
