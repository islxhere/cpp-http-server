#include "core/event_loop_thread_pool.h"

#include "core/event_loop.h"
#include "core/event_loop_thread.h"

namespace httpserver {

EventLoopThreadPool::EventLoopThreadPool(EventLoop* base_loop)
    : base_loop_(base_loop),
      started_(false),
      num_threads_(0),
      next_(0) {}

EventLoopThreadPool::~EventLoopThreadPool() = default;

void EventLoopThreadPool::setThreadNum(int num_threads) {
    num_threads_ = num_threads;
}

void EventLoopThreadPool::start() {
    base_loop_->assertInLoopThread();
    started_ = true;

    for (int i = 0; i < num_threads_; ++i) {
        auto t = std::make_unique<EventLoopThread>();
        loops_.push_back(t->startLoop());
        threads_.push_back(std::move(t));
    }
}

EventLoop* EventLoopThreadPool::getNextLoop() {
    base_loop_->assertInLoopThread();
    EventLoop* loop = base_loop_;

    if (!loops_.empty()) {
        loop = loops_[next_];
        next_ = static_cast<int>((next_ + 1) % loops_.size());
    }

    return loop;
}

}  // namespace httpserver
