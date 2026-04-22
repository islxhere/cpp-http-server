#pragma once

// EventLoop — Reactor 模式的核心，驱动事件循环。
// 每个线程最多只能有一个 EventLoop 对象。
// 职责：调用 Poller 获取活跃事件，遍历并分发给对应 Channel 处理。

#include <sys/types.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "core/channel.h"

namespace httpserver {

class Channel;
class Poller;

class EventLoop {
public:
    using Functor = std::function<void()>;
    using ChannelList = std::vector<Channel*>;

    EventLoop();
    ~EventLoop();

    // 禁止拷贝
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // 事件循环主入口
    void loop();

    // 退出事件循环
    void quit();

    // 在 IO 线程中执行回调，如果当前在 IO 线程则立即执行，
    // 否则放入队列延后执行
    void queueInLoop(Functor cb);

    // Channel 的注册/注销，转发给 Poller
    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

    // 断言当前线程是 EventLoop 所在线程
    void assertInLoopThread() const;

    bool isInLoopThread() const;

private:
    void doPendingFunctors();
    void handleRead();       // 读取 wakeup_fd_ 唤醒 epoll
    void wakeup();           // 向 wakeup_fd_ 写入数据唤醒 epoll

    std::atomic<bool> looping_;
    std::atomic<bool> quit_;
    pid_t thread_id_;

    std::unique_ptr<Poller> poller_;
    ChannelList active_channels_;

    int wakeup_fd_;                              // eventfd，用于跨线程唤醒
    std::unique_ptr<Channel> wakeup_channel_;    // 监听 wakeup_fd_

    std::mutex mutex_;
    std::vector<Functor> pending_functors_;
};

}  // namespace httpserver
