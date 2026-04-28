#pragma once

// TimerQueue — 管理所有 Timer，将时间事件转化为 IO 事件融入 EventLoop。
// 内部使用 timerfd + Channel 将定时器超时转化为 epoll 可读事件。
// timers_ 按过期时间排序，支持高效获取最近的超时事件。

#include <sys/timerfd.h>

#include <chrono>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "utils/timer.h"

namespace httpserver {

class EventLoop;
class Channel;

class TimerQueue {
public:
    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();

    TimerQueue(const TimerQueue&) = delete;
    TimerQueue& operator=(const TimerQueue&) = delete;

    // 添加定时器，线程安全
    void addTimer(Timer* timer);

private:
    using TimerEntry = std::pair<Timer::TimePoint, Timer*>;
    using TimerList = std::set<TimerEntry>;

    // timerfd 可读时的回调
    void handleRead();

    // 重新配置 timerfd_settime，以 timers_ 中最早过期的 Timer 为准
    void resetTimerfd();

    // 将 time_point 转换为 timespec
    static struct timespec howMuchTimeFromNow(Timer::TimePoint when);

    EventLoop* loop_;
    int timerfd_;
    std::unique_ptr<Channel> timerfd_channel_;
    TimerList timers_;
};

}  // namespace httpserver
