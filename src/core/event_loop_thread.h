#pragma once

// EventLoopThread — 启动一个独立线程运行 EventLoop。
// 用于主从 Reactor 模式中的 Sub Reactor 线程。
// 调用 startLoop() 后阻塞等待，直到子线程的 EventLoop 创建完毕，
// 返回该 EventLoop 指针供上层使用。

#include <condition_variable>
#include <mutex>
#include <thread>

namespace httpserver {

class EventLoop;

class EventLoopThread {
public:
    EventLoopThread();
    ~EventLoopThread();

    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

    // 启动子线程并返回其中创建的 EventLoop 指针。
    // 主线程会阻塞在此，直到子线程的 EventLoop 初始化完成。
    EventLoop* startLoop();

private:
    // 在子线程中运行的函数
    void threadFunc();

    EventLoop* loop_;
    bool exiting_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

}  // namespace httpserver
