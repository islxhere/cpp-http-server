#include "core/event_loop.h"

#include <cassert>
#include <stdexcept>
#include <unistd.h>

#include "core/channel.h"
#include "core/poller.h"

namespace httpserver {

// 每个线程最多一个 EventLoop，用 thread_local 记录
static thread_local EventLoop* t_loop_in_this_thread = nullptr;

EventLoop::EventLoop()
    : looping_(false),
      quit_(false),
      thread_id_(::gettid()),
      poller_(std::make_unique<Poller>()) {
    if (t_loop_in_this_thread != nullptr) {
        throw std::runtime_error("Another EventLoop exists in this thread");
    }
    t_loop_in_this_thread = this;
}

EventLoop::~EventLoop() {
    assert(!looping_);
    t_loop_in_this_thread = nullptr;
}

void EventLoop::loop() {
    assert(!looping_);
    looping_ = true;
    quit_ = false;

    while (!quit_) {
        active_channels_.clear();
        poller_->poll(10000, &active_channels_);

        for (Channel* channel : active_channels_) {
            channel->handleEvent();
        }

        // 执行队列中延后的回调
        doPendingFunctors();
    }

    looping_ = false;
}

void EventLoop::quit() {
    quit_ = true;
}

void EventLoop::updateChannel(Channel* channel) {
    assertInLoopThread();
    poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel) {
    assertInLoopThread();
    poller_->removeChannel(channel);
}

void EventLoop::queueInLoop(Functor cb) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_functors_.push_back(std::move(cb));
    }
}

void EventLoop::doPendingFunctors() {
    std::vector<Functor> functors;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        functors.swap(pending_functors_);
    }
    for (auto& functor : functors) {
        functor();
    }
}

void EventLoop::assertInLoopThread() const {
    if (!isInLoopThread()) {
        throw std::runtime_error("EventLoop is not in the creating thread");
    }
}

bool EventLoop::isInLoopThread() const {
    return thread_id_ == ::gettid();
}

}  // namespace httpserver
