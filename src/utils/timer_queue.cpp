#include "utils/timer_queue.h"

#include <unistd.h>

#include "core/channel.h"
#include "core/event_loop.h"

namespace httpserver {

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop),
      timerfd_(::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)),
      timerfd_channel_(std::make_unique<Channel>(loop, timerfd_)) {
    timerfd_channel_->setReadCallback([this]() { handleRead(); });
    timerfd_channel_->enableReading();
}

TimerQueue::~TimerQueue() {
    timerfd_channel_->disableAll();
    timerfd_channel_->remove();
    ::close(timerfd_);
}

void TimerQueue::addTimer(Timer* timer) {
    // 通过 queueInLoop 保证线程安全
    loop_->queueInLoop([this, timer]() {
        // 插入 timers_ 集合
        bool earliest_changed = false;
        if (timers_.empty() || timer->expiration() < timers_.begin()->first) {
            earliest_changed = true;
        }
        timers_.insert({timer->expiration(), timer});

        // 如果新定时器是最早过期的，重新配置 timerfd
        if (earliest_changed) {
            resetTimerfd();
        }
    });
}

void TimerQueue::handleRead() {
    // 读掉 timerfd 中的数据，否则 epoll 会一直触发
    uint64_t expirations = 0;
    ssize_t n = ::read(timerfd_, &expirations, sizeof(expirations));
    (void)n;

    Timer::TimePoint now = Timer::Clock::now();

    // 收集所有已过期的定时器
    std::vector<TimerEntry> expired;
    auto it = timers_.begin();
    while (it != timers_.end() && it->first <= now) {
        expired.push_back(*it);
        it = timers_.erase(it);
    }

    // 执行回调，并处理重复定时器
    for (auto& entry : expired) {
        entry.second->run();

        if (entry.second->repeat()) {
            entry.second->restart(now);
            timers_.insert(TimerEntry{entry.second->expiration(), entry.second});
        } else {
            // 一次性定时器，释放内存
            delete entry.second;
        }
    }

    // 重新配置 timerfd
    if (!timers_.empty()) {
        resetTimerfd();
    }
}

void TimerQueue::resetTimerfd() {
    if (timers_.empty()) return;

    Timer::TimePoint earliest = timers_.begin()->first;
    struct timespec ts = howMuchTimeFromNow(earliest);

    struct itimerspec new_value{};
    new_value.it_value = ts;
    ::timerfd_settime(timerfd_, 0, &new_value, nullptr);
}

struct timespec TimerQueue::howMuchTimeFromNow(Timer::TimePoint when) {
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        when - Timer::Clock::now());
    if (us.count() < 100) {
        us = std::chrono::microseconds(100);
    }
    struct timespec ts{};
    ts.tv_sec = static_cast<time_t>(us.count() / 1000000);
    ts.tv_nsec = static_cast<long>((us.count() % 1000000) * 1000);
    return ts;
}

}  // namespace httpserver
