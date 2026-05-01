#include "core/event_loop.h"

#include <cassert>
#include <stdexcept>
#include <unistd.h>

#include <sys/eventfd.h>

#include "core/poller.h"
#include "utils/timer.h"
#include "utils/timer_queue.h"

namespace httpserver {

// 每个线程最多一个 EventLoop，用 thread_local 记录
static thread_local EventLoop* t_loop_in_this_thread = nullptr;

EventLoop::EventLoop()
    : looping_(false),
      quit_(false),
      thread_id_(::gettid()),
      poller_(std::make_unique<Poller>()),
      timer_queue_(std::make_unique<TimerQueue>(this)),
      wakeup_fd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
      wakeup_channel_(std::make_unique<Channel>(this, wakeup_fd_)) {
    if (t_loop_in_this_thread != nullptr) {
        throw std::runtime_error("Another EventLoop exists in this thread");
    }
    t_loop_in_this_thread = this;

    // 监听 wakeup_fd_ 的可读事件
    wakeup_channel_->setReadCallback([this]() { handleRead(); });
    wakeup_channel_->enableReading();
}

EventLoop::~EventLoop() {
    assert(!looping_);
    wakeup_channel_->disableAll();
    wakeup_channel_->remove();
    ::close(wakeup_fd_);
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
    wakeup();
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
    // 总是唤醒 epoll，确保 pending functor 及时执行。
    // 即使从 loop 线程调用，也可能在 poll() 阻塞期间排队。
    wakeup();
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

void EventLoop::wakeup() {
    uint64_t one = 1;
    ssize_t n = ::write(wakeup_fd_, &one, sizeof(one));
    (void)n;
}

void EventLoop::handleRead() {
    uint64_t one = 0;
    ssize_t n = ::read(wakeup_fd_, &one, sizeof(one));
    (void)n;
}

void EventLoop::runAt(std::chrono::steady_clock::time_point time, TimerCallback cb) {
    auto* timer = new Timer(std::move(cb), time);
    timer_queue_->addTimer(timer);
}

void EventLoop::runAfter(double delay_seconds, TimerCallback cb) {
    auto time = std::chrono::steady_clock::now() +
                std::chrono::microseconds(static_cast<int64_t>(delay_seconds * 1000000));
    runAt(time, std::move(cb));
}

void EventLoop::runEvery(double interval_seconds, TimerCallback cb) {
    auto time = std::chrono::steady_clock::now() +
                std::chrono::microseconds(static_cast<int64_t>(interval_seconds * 1000000));
    auto* timer = new Timer(std::move(cb), time, interval_seconds);
    timer_queue_->addTimer(timer);
}

}  // namespace httpserver
