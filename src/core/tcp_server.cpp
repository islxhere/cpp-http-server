#include "core/tcp_server.h"

#include <unistd.h>

#include <chrono>
#include <sstream>

#include "core/acceptor.h"
#include "core/event_loop.h"
#include "core/event_loop_thread_pool.h"
#include "core/tcp_connection.h"

namespace httpserver {

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listen_addr)
    : loop_(loop),
      acceptor_(std::make_unique<Acceptor>(loop, listen_addr)),
      thread_pool_(std::make_unique<EventLoopThreadPool>(loop)) {
    acceptor_->setNewConnectionCallback(
        [this](int sockfd, const InetAddress& peer_addr) {
            newConnection(sockfd, peer_addr);
        });
}

TcpServer::~TcpServer() = default;

void TcpServer::setThreadNum(int num_threads) {
    thread_pool_->setThreadNum(num_threads);
}

void TcpServer::start() {
    thread_pool_->start();
    acceptor_->listen();

    // 每秒检测一次空闲连接
    if (idle_timeout_ms_ > 0) {
        loop_->runEvery(1.0, [this]() { checkIdleConnections(); });
    }
}

void TcpServer::setIdleTimeout(int timeout_ms) {
    idle_timeout_ms_ = timeout_ms;
}

void TcpServer::setConnectionCallback(ConnectionCallback cb) {
    connection_callback_ = std::move(cb);
}

void TcpServer::setMessageCallback(MessageCallback cb) {
    message_callback_ = std::move(cb);
}

void TcpServer::newConnection(int sockfd, const InetAddress& peer_addr) {
    // 从线程池获取一个 SubLoop
    EventLoop* io_loop = thread_pool_->getNextLoop();

    std::string name = peer_addr.ipPort();
    InetAddress local_addr;

    auto conn = std::make_shared<TcpConnection>(io_loop, sockfd, local_addr, peer_addr);
    connections_[name] = conn;

    conn->setMessageCallback(message_callback_);
    conn->setCloseCallback(
        [this](const TcpConnectionPtr& c) { removeConnection(c); });

    // 通知上层新连接建立（如 HttpServer 绑定 HttpContext）
    if (connection_callback_) {
        connection_callback_(conn);
    }

    // 在 SubLoop 线程中完成连接建立（注册 Channel 到 SubLoop 的 Poller）
    io_loop->queueInLoop([conn]() { conn->connectEstablished(); });
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn) {
    EventLoop* io_loop = conn->getLoop();

    // 从 MainLoop 的 connections_ 字典中移除（线程安全：queueInLoop 到 MainLoop）
    loop_->queueInLoop([this, conn]() {
        connections_.erase(conn->name());
    });

    // 在 SubLoop 线程中销毁连接（关闭 Channel、释放 fd）
    io_loop->queueInLoop([conn]() { conn->connectDestroyed(); });
}

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

}  // namespace httpserver
