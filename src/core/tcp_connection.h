#pragma once

// TcpConnection — 代表一条已建立的 TCP 连接。
// 生命周期由 shared_ptr 管理，可安全跨线程传递。
// 内部持有 Channel 监听读写事件，以及输入输出 Buffer。

#include <any>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "core/buffer.h"
#include "core/inet_address.h"

namespace httpserver {

class EventLoop;
class Channel;
class InetAddress;

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
    using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*)>;
    using CloseCallback = std::function<void(const TcpConnectionPtr&)>;
    using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;

    TcpConnection(EventLoop* loop, int conn_fd, const InetAddress& local_addr,
                  const InetAddress& peer_addr);
    ~TcpConnection();

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    void setMessageCallback(MessageCallback cb);
    void setCloseCallback(CloseCallback cb);
    void setWriteCompleteCallback(WriteCompleteCallback cb);

    void send(const std::string& message);

    void shutdown();
    void forceClose();

    void connectEstablished();
    void connectDestroyed();

    const std::string& name() const;
    EventLoop* getLoop() const;
    Buffer* inputBuffer();
    Buffer* outputBuffer();
    int fd() const;

    // 用于绑定每连接上下文（如 HttpContext）
    void setContext(const std::any& context);
    const std::any& getContext() const;
    std::any& mutableContext();

    // 最近活跃时间（用于空闲连接检测）
    using Clock = std::chrono::steady_clock;
    Clock::time_point lastActiveTime() const;

private:
    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();

    EventLoop* loop_;
    int conn_fd_;
    std::string name_;
    std::unique_ptr<Channel> channel_;
    Buffer input_buffer_;
    Buffer output_buffer_;

    MessageCallback message_callback_;
    CloseCallback close_callback_;
    WriteCompleteCallback write_complete_callback_;
    std::any context_;
    Clock::time_point last_active_time_;
};

}  // namespace httpserver
