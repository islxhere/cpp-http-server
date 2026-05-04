# 阶段二：HTTP 协议层 — 开发日志

> 项目：高并发 HTTP/1.1 服务器（C++17 / Linux epoll）
> 日期：2026-05-02
> 状态：全部完成，测试通过

---

## 一、阶段概述

阶段二在阶段一的单线程 Reactor 网络层之上，实现了完整的 HTTP/1.1 协议处理层，共 5 个模块（10 个源文件），总计约 608 行 C++ 代码。

### 完成的模块

| # | 模块 | 文件 | 行数 | 说明 |
|---|------|------|------|------|
| 1 | HttpRequest | `http_request.h/cpp` | 101 | HTTP 请求数据结构 |
| 2 | HttpResponse | `http_response.h/cpp` | 130 | HTTP 响应构造与报文序列化 |
| 3 | HttpParser | `http_parser.h/cpp` | 99 | 静态解析方法（请求行 + 头部） |
| 4 | HttpContext | `http_context.h/cpp` | 158 | 状态机驱动的 HTTP 解析器 |
| 5 | HttpServer | `http_server.h/cpp` | 143 | 在 TcpServer 上封装 HTTP 语义 |

### 测试文件

| 测试文件 | 测试数 | 覆盖模块 |
|----------|--------|----------|
| `test_http_request_response.cpp` | 6+8=14 | HttpRequest + HttpResponse |
| `test_http_parser.cpp` | 5 | HttpContext + HttpParser |
| **合计** | **19** | |

---

## 二、架构设计

### 2.1 分层架构

```
┌─────────────────────────────────────────────────┐
│                  用户回调                         │
│   setHttpCallback([](req, resp) { ... })         │
└──────────┬──────────────────────────────────────┘
           │
           ▼
┌─────────────────────────────────────────────────┐
│                 HttpServer                        │
│  onConnection() → 绑定 HttpContext（std::any）    │
│  onMessage() → 驱动 HttpContext::parseRequest()   │
│  解析完成 → 调用 http_callback_ → 生成响应        │
└──────────┬──────────────────────────────────────┘
           │ 使用
           ▼
┌─────────────────────────────────────────────────┐
│               HttpContext（状态机）                │
│  kExpectRequestLine → kExpectHeaders              │
│  → kExpectBody → kGotAll                          │
│  持有 HttpRequest，逐步填充解析结果                │
└──────────┬──────────────────────────────────────┘
           │ 调用
           ▼
┌─────────────────────────────────────────────────┐
│       HttpParser（静态方法）+ HttpRequest          │
│  parseRequestLine() — 解析 GET /path HTTP/1.1    │
│  parseHeader() — 解析 Key: Value                 │
└─────────────────────────────────────────────────┘
```

### 2.2 HTTP 请求处理数据流

```
客户端发送 HTTP 请求数据
  → TcpConnection::handleRead() 读入 Buffer
  → HttpServer::onMessage(conn, buf)
  → HttpContext::parseRequest(buf)
      → 状态机推进：
         kExpectRequestLine: findCRLF → HttpParser::parseRequestLine()
         kExpectHeaders:     findCRLF → HttpParser::parseHeader()
                             遇到空行 → kGotAll
  → 解析完成，调用 http_callback_(request, response)
  → HttpResponse::appendToBuffer() 序列化响应
  → conn->send() 发送响应
```

---

## 三、各模块详细设计

### 3.1 HttpRequest — 请求数据结构

**职责**：存储解析后的 HTTP 请求数据。

**设计要点**：
- 支持 GET / POST / PUT / DELETE 四种方法
- 支持 HTTP/1.0 和 HTTP/1.1 两个版本
- `path_` 和 `query_` 分离存储（`/index.html?name=test` → path=`/index.html`, query=`name=test`）
- headers 使用 `unordered_map<string, string>` 存储
- body 使用 `string` 存储，支持 `appendBody()` 分段追加

**接口**：
```cpp
class HttpRequest {
    enum class Method { kInvalid, kGet, kPost, kPut, kDelete };
    enum class Version { kUnknown, kHttp10, kHttp11 };

    void setMethod(Method method);
    void setPath(const std::string& path);
    void setQuery(const std::string& query);
    void addHeader(const std::string& key, const std::string& value);
    void setBody(const std::string& body);
    // ... 对应的 getter
};
```

### 3.2 HttpResponse — 响应构造

**职责**：构造 HTTP 响应并序列化为字节流。

**设计要点**：
- `appendToBuffer()` 将完整的 HTTP 响应写入 Buffer，格式为：
  ```
  HTTP/1.1 200 OK\r\n
  Content-Type: text/html\r\n
  Content-Length: 13\r\n
  \r\n
  Hello, World!
  ```
- `close_connection_` 标志控制是否添加 `Connection: close` 头
- 支持 200 OK、400 Bad Request、404 Not Found 等状态码

**序列化逻辑**：
```cpp
void HttpResponse::appendToBuffer(Buffer* output) const {
    // 状态行
    output->append("HTTP/1.1 " + to_string(status_code_) + " " + status_message_ + "\r\n");
    // 响应头
    for (auto& [key, value] : headers_) {
        output->append(key + ": " + value + "\r\n");
    }
    // 空行 + 响应体
    output->append("\r\n");
    output->append(body_);
}
```

### 3.3 HttpParser — 静态解析方法

**职责**：提供无状态的 HTTP 报文解析方法。

**设计要点**：
- 所有方法为 `static`，不持有任何状态
- `parseRequestLine()` 解析 `METHOD /path?query HTTP/1.x` 格式
- `parseHeader()` 解析 `Key: Value` 格式
- 返回 `bool` 表示解析成功或格式错误

**请求行解析**：
```cpp
// "GET /index.html?name=test HTTP/1.1"
//  ↓ method    ↓ path       ↓ query    ↓ version
//  "GET"       "/index.html" "name=test" "HTTP/1.1"
```

### 3.4 HttpContext — 状态机

**职责**：维护单条连接的 HTTP 解析状态，驱动状态机逐步解析。

**状态机流转**：
```
kExpectRequestLine
    │ 解析到 \r\n → parseRequestLine()
    ▼
kExpectHeaders
    │ 解析到 \r\n → parseHeader()
    │ 解析到空行（\r\n\r\n）→ 检查 Content-Length
    ▼
kExpectBody（如果有 body）
    │ 读取足够字节
    ▼
kGotAll
```

**设计要点**：
- `findCRLF()` 使用 `std::search` 在 Buffer 中查找 `\r\n`
- **关键设计**：不消费 Buffer 数据，直到找到完整的行。这保证了半包场景下数据不会丢失
- `parseRequest()` 每次调用可能推进多个状态（一行一个状态转换）
- `reset()` 重置状态机，用于 Keep-Alive 连接复用

**半包处理**：
```cpp
bool HttpContext::parseRequest(Buffer* buf) {
    while (state_ != kGotAll) {
        const char* crlf = findCRLF(buf->peek(), buf->beginWrite());
        if (crlf == buf->beginWrite()) {
            // 没找到 \r\n，等待更多数据
            break;
        }
        // 找到一行，解析并消费
        bool ok = parseLine(...);
        if (!ok) return false;
    }
    return true;
}
```

### 3.5 HttpServer — HTTP 服务封装

**职责**：在 TcpServer 基础上封装 HTTP 语义，用户只需设置一个回调函数。

**设计要点**：
- 持有 `unique_ptr<TcpServer>`，代理 start / setThreadNum 等方法
- `onConnection()` 通过 `std::any` 为每条连接绑定 HttpContext
- `onMessage()` 驱动 HttpContext 解析，完成后调用用户回调
- 支持 Keep-Alive：解析完成后调用 `ctx.reset()` 重置状态机

**连接上下文绑定**：
```cpp
void HttpServer::onConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        conn->setContext(HttpContext());  // 绑定 HttpContext
    }
}

void HttpServer::onMessage(const TcpConnectionPtr& conn, Buffer* buf) {
    auto ctx = std::any_cast<HttpContext>(conn->mutableContext());
    if (!ctx->parseRequest(buf)) {
        conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
        conn->shutdown();
        return;
    }
    if (ctx->state() == HttpContext::ParseState::kGotAll) {
        // 调用用户回调生成响应
        HttpResponse resp(ctx->request().version() == HttpRequest::Version::kHttp10);
        http_callback_(ctx->request(), &resp);
        resp.appendToBuffer(conn->outputBuffer());
        conn->send(...);
        ctx->reset();  // Keep-Alive 复用
    }
}
```

---

## 四、关键技术决策

### 4.1 为什么 HttpParser 和 HttpContext 分离？

HttpParser 是无状态的纯函数，只负责解析单行；HttpContext 是有状态的，负责驱动整个解析流程。这种分离使得：
- HttpParser 可以独立单元测试
- HttpContext 可以复用 HttpParser 的解析逻辑
- 未来扩展（如 chunked encoding）只需修改 HttpContext 的状态机

### 4.2 为什么用 std::any 绑定连接上下文？

`std::any` 允许在 TcpConnection 上绑定任意类型的上下文，而不侵入 TcpConnection 的接口。HttpServer 在 `onConnection` 时绑定 HttpContext，在 `onMessage` 时取出使用。这种方式使得网络层和 HTTP 层完全解耦。

### 4.3 为什么不消费 Buffer 数据直到完整行找到？

在半包场景下，如果先消费了部分数据但后续数据还没到达，这部分数据就丢失了。通过 `findCRLF` 只查找不消费，只有在找到完整的 `\r\n` 后才调用 `retrieve` 消费数据，保证了数据安全。

### 4.4 为什么 HttpResponse 构造时需要 close_connection 标志？

HTTP/1.0 默认不支持 Keep-Alive，需要显式声明 `Connection: keep-alive`；HTTP/1.1 默认支持。通过 `close_connection` 标志，HttpServer 可以根据请求版本决定是否关闭连接。

---

## 五、测试验证

### 5.1 单元测试（19 个）

```
[==========] Running 19 tests from 2 test suites.
[  PASSED  ] HttpRequestTest (6 tests)
[  PASSED  ] HttpResponseTest (8 tests)
[  PASSED  ] HttpParserTest (5 tests)
[==========] 19 tests passed.
```

### 5.2 关键测试用例

**半包解析测试**：
```
将完整请求从 headers 中间切断，分两次喂入
第一次解析：解析完 request line，停在 kExpectHeaders
第二次解析：推进到 kGotAll
验证所有字段正确
```

**Keep-Alive 复用测试**：
```
解析第一个 GET 请求 → kGotAll
reset() 重置状态机
解析第二个 POST 请求 → kGotAll
验证两个请求的数据都正确
```

### 5.3 HTTP Server 功能验证

```bash
# 启动服务器
./http_server

# 浏览器访问 http://localhost:8080
# 返回: <h1>Hello! Handled by Thread: xxxxx</h1>

# 访问 http://localhost:8080/other
# 返回: <h1>404 Not Found</h1>
```

---

## 六、与阶段一的衔接

阶段二完全复用阶段一的网络层，零修改：
- HttpServer 内部创建 TcpServer，设置 ConnectionCallback 和 MessageCallback
- TcpConnection 的 `std::any context_` 字段用于绑定 HttpContext
- Buffer 作为 HTTP 数据的载体，在 TcpConnection 和 HttpContext 之间传递
- main.cpp 从 Echo Server 升级为 HTTP Server，仅修改回调逻辑

---

## 七、文件清单

```
src/http/
├── http_request.h      (58 行)
├── http_request.cpp     (43 行)
├── http_response.h     (52 行)
├── http_response.cpp    (78 行)
├── http_parser.h       (23 行)
├── http_parser.cpp      (76 行)
├── http_context.h      (44 行)
├── http_context.cpp    (114 行)
├── http_server.h       (45 行)
└── http_server.cpp      (98 行)

src/
└── main.cpp             (48 行) — HTTP Server

tests/
├── test_http_request_response.cpp (197 行)
└── test_http_parser.cpp           (164 行)

总计：约 608 行 C++ 代码（HTTP 层）+ 361 行测试
```

---

## 八、后续计划

阶段二完成后，项目将进入阶段三（多线程主从 Reactor）：
- `EventLoopThread` — 启动独立线程运行 EventLoop
- `EventLoopThreadPool` — 管理 Sub Reactor 线程池
- `TcpServer` 改造 — 支持多线程连接分发
