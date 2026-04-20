#pragma once

// TcpServer — 服务端 TCP 服务器。
// 管理 Acceptor 和所有 TcpConnection 的生命周期。
// 用户通过设置 ConnectionCallback 和 MessageCallback 来处理业务逻辑。

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "core/inet_address.h"

namespace httpserver {

class EventLoop;
class Acceptor;
class TcpConnection;
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

    void start();

    void setConnectionCallback(ConnectionCallback cb);
    void setMessageCallback(MessageCallback cb);

private:
    void newConnection(int sockfd, const InetAddress& peer_addr);
    void removeConnection(const TcpConnectionPtr& conn);
    void destroyConnection(const TcpConnectionPtr& conn);

    EventLoop* loop_;
    std::unique_ptr<Acceptor> acceptor_;
    std::unordered_map<std::string, TcpConnectionPtr> connections_;

    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;
};

}  // namespace httpserver
