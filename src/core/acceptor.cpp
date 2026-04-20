#include "core/acceptor.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>

#include "core/channel.h"
#include "core/event_loop.h"

namespace httpserver {

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listen_addr)
    : loop_(loop),
      listen_fd_(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)),
      accept_channel_(loop, listen_fd_) {
    if (listen_fd_ < 0) {
        throw std::runtime_error(std::string("socket failed: ") + std::strerror(errno));
    }

    int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    const auto& addr = listen_addr.getSockAddr();
    if (::bind(listen_fd_, reinterpret_cast<const struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd_);
        throw std::runtime_error(std::string("bind failed: ") + std::strerror(errno));
    }
}

Acceptor::~Acceptor() {
    accept_channel_.disableAll();
    accept_channel_.remove();
    ::close(listen_fd_);
}

void Acceptor::listen() {
    if (::listen(listen_fd_, SOMAXCONN) < 0) {
        throw std::runtime_error(std::string("listen failed: ") + std::strerror(errno));
    }
    accept_channel_.setReadCallback([this]() { handleRead(); });
    accept_channel_.enableReading();
}

void Acceptor::setNewConnectionCallback(NewConnectionCallback cb) {
    new_connection_callback_ = std::move(cb);
}

void Acceptor::handleRead() {
    struct sockaddr_in peer_addr{};
    socklen_t addr_len = sizeof(peer_addr);

    // 循环 accept，直到没有更多连接（非阻塞）
    while (true) {
        int conn_fd = ::accept4(listen_fd_,
                                reinterpret_cast<struct sockaddr*>(&peer_addr),
                                &addr_len,
                                SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (conn_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // 没有更多连接
            }
            throw std::runtime_error(std::string("accept4 failed: ") + std::strerror(errno));
        }

        if (new_connection_callback_) {
            InetAddress peer(peer_addr);
            new_connection_callback_(conn_fd, peer);
        } else {
            ::close(conn_fd);
        }
    }
}

}  // namespace httpserver
