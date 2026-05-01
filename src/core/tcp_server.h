#pragma once

// TcpServer — 服务端 TCP 服务器（主从 Reactor 架构）。
// 管理 Acceptor（MainLoop）和所有 TcpConnection（SubLoop）的生命周期。
// 新连接通过 EventLoopThreadPool 分配到 SubLoop 线程处理。

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "core/inet_address.h"

namespace httpserver {

class EventLoop;
class Acceptor;
class TcpConnection;
class EventLoopThreadPool;
class Buffer;

class TcpServer {
public:
    using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
    using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
    using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*)>;

    TcpServer(EventLoop* loop, const InetAddress& listen_addr);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    void setThreadNum(int num_threads);
    void start();

    void setConnectionCallback(ConnectionCallback cb);
    void setMessageCallback(MessageCallback cb);

    // 空闲连接超时（毫秒），默认 8000ms，0 表示不检测
    void setIdleTimeout(int timeout_ms);

private:
    void newConnection(int sockfd, const InetAddress& peer_addr);
    void removeConnection(const TcpConnectionPtr& conn);
    void checkIdleConnections();

    EventLoop* loop_;
    std::unique_ptr<Acceptor> acceptor_;
    std::unique_ptr<EventLoopThreadPool> thread_pool_;
    std::unordered_map<std::string, TcpConnectionPtr> connections_;

    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;
    int idle_timeout_ms_ = 8000;
};

}  // namespace httpserver
