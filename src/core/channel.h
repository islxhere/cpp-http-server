#pragma once

// Channel — 管理一个文件描述符的 IO 事件分发。
// 不拥有 fd（不负责 close），只负责注册/注销事件、分发回调。
// 生命周期由所属的 EventLoop 管理。

#include <sys/epoll.h>

#include <functional>

namespace httpserver {

class EventLoop;  // 前向声明，后续实现 EventLoop 时再补充

class Channel {
public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    // 禁止拷贝
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    // 事件处理：根据 revents_ 调用对应回调
    void handleEvent();

    // 启用/禁用事件
    void enableReading();
    void disableReading();
    void enableWriting();
    void disableWriting();
    void disableAll();

    // fd 状态查询
    bool isNoneEvent() const;

    // getter / setter
    int fd() const;
    uint32_t events() const;
    uint32_t revents() const;
    void setRevents(uint32_t revents);
    EventLoop* ownerLoop() const;

    // 从 EventLoop 中移除自身
    void remove();

    // 设置回调
    void setReadCallback(EventCallback cb);
    void setWriteCallback(EventCallback cb);
    void setCloseCallback(EventCallback cb);
    void setErrorCallback(EventCallback cb);

private:
    void update();

    EventLoop* event_loop_;
    int fd_;
    uint32_t events_;   // 关注的事件
    uint32_t revents_;  // 实际发生的事件（由 Poller 填充）

    EventCallback read_callback_;
    EventCallback write_callback_;
    EventCallback close_callback_;
    EventCallback error_callback_;

    // 事件掩码
    static constexpr uint32_t kNoneEvent  = 0;
    static constexpr uint32_t kReadEvent  = EPOLLIN | EPOLLPRI;
    static constexpr uint32_t kWriteEvent = EPOLLOUT;
};

}  // namespace httpserver
