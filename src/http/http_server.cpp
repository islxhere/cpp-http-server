#include "http/http_server.h"

#include <any>

#include "core/buffer.h"
#include "core/tcp_connection.h"
#include "core/tcp_server.h"
#include "http/http_context.h"

namespace httpserver {

HttpServer::HttpServer(EventLoop* loop, const InetAddress& listen_addr)
    : server_(std::make_unique<TcpServer>(loop, listen_addr)) {
    server_->setConnectionCallback(
        [this](const TcpConnectionPtr& conn) { onConnection(conn); });
    server_->setMessageCallback(
        [this](const TcpConnectionPtr& conn, Buffer* buf) {
            onMessage(conn, buf);
        });
}

HttpServer::~HttpServer() = default;

void HttpServer::start() { server_->start(); }

void HttpServer::setHttpCallback(const HttpCallback& cb) {
    http_callback_ = std::move(cb);
}

void HttpServer::onConnection(const TcpConnectionPtr& conn) {
    // 新连接建立时，绑定一个 HttpContext
    conn->setContext(HttpContext());
}

void HttpServer::onMessage(const TcpConnectionPtr& conn, Buffer* buf) {
    if (!conn->getContext().has_value()) {
        return;
    }
    auto& context = std::any_cast<HttpContext&>(conn->mutableContext());

    if (!context.parseRequest(buf)) {
        // 解析出错，返回 400 Bad Request
        HttpResponse response(true);
        response.setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
        response.setStatusMessage("Bad Request");
        response.setContentType("text/html");
        response.setBody("<h1>400 Bad Request</h1>");

        Buffer response_buf;
        response.appendToBuffer(&response_buf);
        conn->send(response_buf.retrieveAllAsString());
        conn->forceClose();
        return;
    }

    if (context.state() == HttpContext::ParseState::kGotAll) {
        // 解析完成，构造响应
        const HttpRequest& request = context.request();

        // 判断是否需要关闭连接
        bool close = false;
        if (request.version() == HttpRequest::Version::kHttp10) {
            close = true;
        } else {
            std::string connection_header = request.getHeader("Connection");
            if (connection_header == "close") {
                close = true;
            }
        }

        HttpResponse response(close);

        // 触发用户回调
        if (http_callback_) {
            http_callback_(request, &response);
        }

        // 序列化响应并发送
        Buffer response_buf;
        response.appendToBuffer(&response_buf);
        conn->send(response_buf.retrieveAllAsString());

        if (close) {
            // Connection: close，发送完后关闭
            conn->shutdown();
        } else {
            // Keep-Alive，重置 context 准备接收下一个请求
            context.reset();
        }
    }
    // 否则还在等待更多数据，不做任何处理
}

}  // namespace httpserver
