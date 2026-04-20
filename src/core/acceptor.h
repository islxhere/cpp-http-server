#pragma once

// Acceptor — 负责监听端口、接受新连接。
// 内部持有 listen fd 和一个 Channel，当有新连接到来时
// 通过回调通知上层（TcpServer）。

#include <functional>

#include "core/channel.h"
#include "core/inet_address.h"

namespace httpserver {

class EventLoop;

class Acceptor {
public:
    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress&)>;

    Acceptor(EventLoop* loop, const InetAddress& listen_addr);
    ~Acceptor();

    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;

    void listen();

    void setNewConnectionCallback(NewConnectionCallback cb);

private:
    void handleRead();

    EventLoop* loop_;
    int listen_fd_;
    Channel accept_channel_;
    NewConnectionCallback new_connection_callback_;
};

}  // namespace httpserver
