#include "core/tcp_connection.h"

#include <cerrno>
#include <cstring>
#include <unistd.h>

#include <sys/socket.h>

#include "core/channel.h"
#include "core/event_loop.h"

namespace httpserver {

TcpConnection::TcpConnection(EventLoop* loop, int conn_fd,
                             const InetAddress& /*local_addr*/,
                             const InetAddress& peer_addr)
    : loop_(loop),
      conn_fd_(conn_fd),
      name_(peer_addr.ipPort()),
      channel_(std::make_unique<Channel>(loop, conn_fd)) {
    channel_->setReadCallback([this]() { handleRead(); });
    channel_->setWriteCallback([this]() { handleWrite(); });
    channel_->setCloseCallback([this]() { handleClose(); });
    channel_->setErrorCallback([this]() { handleError(); });
}

TcpConnection::~TcpConnection() = default;

void TcpConnection::setMessageCallback(MessageCallback cb) {
    message_callback_ = std::move(cb);
}

void TcpConnection::setCloseCallback(CloseCallback cb) {
    close_callback_ = std::move(cb);
}

void TcpConnection::setWriteCompleteCallback(WriteCompleteCallback cb) {
    write_complete_callback_ = std::move(cb);
}

void TcpConnection::send(const std::string& message) {
    ssize_t nwrote = 0;
    size_t remaining = message.size();

    // 如果输出缓冲区为空，先尝试直接写
    if (output_buffer_.readableBytes() == 0) {
        nwrote = ::write(conn_fd_, message.data(), message.size());
        if (nwrote < 0) {
            nwrote = 0;
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                handleClose();
                return;
            }
        }
        remaining = message.size() - nwrote;
        if (remaining == 0 && write_complete_callback_) {
            write_complete_callback_(shared_from_this());
            return;
        }
    }

    // 还有剩余数据，追加到输出缓冲区并使能写事件
    if (remaining > 0) {
        output_buffer_.append(message.data() + nwrote, remaining);
        channel_->enableWriting();
    }
}

void TcpConnection::shutdown() {
    ::shutdown(conn_fd_, SHUT_WR);
}

void TcpConnection::forceClose() {
    handleClose();
}

void TcpConnection::connectEstablished() {
    channel_->enableReading();
}

void TcpConnection::connectDestroyed() {
    channel_->disableAll();
    channel_->remove();
    ::close(conn_fd_);
}

Buffer* TcpConnection::inputBuffer() { return &input_buffer_; }
Buffer* TcpConnection::outputBuffer() { return &output_buffer_; }
int TcpConnection::fd() const { return conn_fd_; }
const std::string& TcpConnection::name() const { return name_; }
EventLoop* TcpConnection::getLoop() const { return loop_; }

void TcpConnection::handleRead() {
    int saved_errno = 0;
    ssize_t n = input_buffer_.readFd(conn_fd_, &saved_errno);
    if (n > 0) {
        if (message_callback_) {
            message_callback_(shared_from_this(), &input_buffer_);
        }
    } else if (n == 0) {
        // 对端关闭连接
        handleClose();
    } else {
        errno = saved_errno;
        handleError();
    }
}

void TcpConnection::handleWrite() {
    ssize_t n = ::write(conn_fd_, output_buffer_.peek(), output_buffer_.readableBytes());
    if (n > 0) {
        output_buffer_.retrieve(n);
        if (output_buffer_.readableBytes() == 0) {
            // 数据全部写完，关闭写事件关注
            channel_->disableWriting();
            if (write_complete_callback_) {
                write_complete_callback_(shared_from_this());
            }
        }
    } else if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            handleClose();
        }
    }
}

void TcpConnection::handleClose() {
    if (close_callback_) {
        close_callback_(shared_from_this());
    }
}

void TcpConnection::handleError() {
    handleClose();
}

void TcpConnection::setContext(const std::any& context) {
    context_ = context;
}

const std::any& TcpConnection::getContext() const {
    return context_;
}

std::any& TcpConnection::mutableContext() {
    return context_;
}

}  // namespace httpserver
