#pragma once

// HttpServer — 在 TcpServer 基础上封装 HTTP 语义。
// 管理 HTTP 服务的生命周期，负责拦截数据调用 HttpContext 解析，
// 解析完成后触发用户注册的 HttpCallback 生成响应。

#include <functional>
#include <memory>

#include "core/inet_address.h"
#include "http/http_request.h"
#include "http/http_response.h"

namespace httpserver {

class EventLoop;
class TcpServer;
class TcpConnection;
class Buffer;

class HttpServer {
public:
    using HttpCallback = std::function<void(const HttpRequest&, HttpResponse*)>;

    HttpServer(EventLoop* loop, const InetAddress& listen_addr);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void setThreadNum(int num_threads);
    void start();
    void setHttpCallback(const HttpCallback& cb);

private:
    using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

    void onConnection(const TcpConnectionPtr& conn);
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf);

    std::unique_ptr<TcpServer> server_;
    HttpCallback http_callback_;
};

}  // namespace httpserver
