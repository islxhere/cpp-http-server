#include "core/event_loop_thread.h"

#include <mutex>

#include "core/event_loop.h"

namespace httpserver {

EventLoopThread::EventLoopThread()
    : loop_(nullptr),
      exiting_(false) {}

EventLoopThread::~EventLoopThread() {
    exiting_ = true;
    if (loop_ != nullptr) {
        loop_->quit();
        thread_.join();
    }
}

EventLoop* EventLoopThread::startLoop() {
    // 启动子线程，执行 threadFunc
    thread_ = std::thread([this]() { threadFunc(); });

    // 阻塞等待子线程创建完 EventLoop
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]() { return loop_ != nullptr; });
    }

    return loop_;
}

void EventLoopThread::threadFunc() {
    // 在子线程栈上创建 EventLoop
    EventLoop loop;

    // 将指针赋给成员变量，唤醒主线程
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }

    // 进入事件循环（阻塞，直到 quit 被调用）
    loop.loop();

    // loop 退出后，清空指针
    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = nullptr;
}

}  // namespace httpserver
