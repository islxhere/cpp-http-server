#include "core/tcp_server.h"

#include <unistd.h>

#include <sstream>

#include "core/acceptor.h"
#include "core/event_loop.h"
#include "core/tcp_connection.h"

namespace httpserver {

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listen_addr)
    : loop_(loop),
      acceptor_(std::make_unique<Acceptor>(loop, listen_addr)) {
    acceptor_->setNewConnectionCallback(
        [this](int sockfd, const InetAddress& peer_addr) {
            newConnection(sockfd, peer_addr);
        });
}

TcpServer::~TcpServer() = default;

void TcpServer::start() {
    acceptor_->listen();
}

void TcpServer::setConnectionCallback(ConnectionCallback cb) {
    connection_callback_ = std::move(cb);
}

void TcpServer::setMessageCallback(MessageCallback cb) {
    message_callback_ = std::move(cb);
}

void TcpServer::newConnection(int sockfd, const InetAddress& peer_addr) {
    // 生成连接名：ip:port
    std::string name = peer_addr.ipPort();

    // 本地地址（TcpConnection 要求传入，当前实现未使用）
    InetAddress local_addr;

    auto conn = std::make_shared<TcpConnection>(loop_, sockfd, local_addr, peer_addr);
    connections_[name] = conn;

    conn->setMessageCallback(message_callback_);
    conn->setCloseCallback(
        [this](const TcpConnectionPtr& c) { removeConnection(c); });

    conn->connectEstablished();
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn) {
    // 延后销毁，避免回调执行到一半对象被 erase
    loop_->queueInLoop([this, conn]() { destroyConnection(conn); });
}

void TcpServer::destroyConnection(const TcpConnectionPtr& conn) {
    connections_.erase(conn->name());
    conn->connectDestroyed();
}

}  // namespace httpserver
