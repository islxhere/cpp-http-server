#pragma once

// EventLoopThreadPool — 管理所有 Sub Reactor 线程。
// 负责创建子线程并运行各自的 EventLoop，
// 通过 Round-Robin 策略将新连接分发给子线程。
// 如果未设置线程数，所有操作回退到主线程的 baseLoop_。

#include <memory>
#include <vector>

namespace httpserver {

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool {
public:
    EventLoopThreadPool(EventLoop* base_loop);
    ~EventLoopThreadPool();

    EventLoopThreadPool(const EventLoopThreadPool&) = delete;
    EventLoopThreadPool& operator=(const EventLoopThreadPool&) = delete;

    void setThreadNum(int num_threads);
    void start();

    // Round-Robin 获取下一个 EventLoop
    EventLoop* getNextLoop();

private:
    EventLoop* base_loop_;
    bool started_;
    int num_threads_;
    int next_;
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
};

}  // namespace httpserver
