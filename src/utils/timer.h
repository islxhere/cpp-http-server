#pragma once

// Timer — 封装单个定时任务。
// 持有过期时间、触发间隔和回调函数。
// 支持一次性定时器（interval_ == 0）和重复定时器。

#include <chrono>
#include <functional>

namespace httpserver {

class Timer {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    Timer(std::function<void()> callback, TimePoint expiration, double interval = 0.0);

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    // 执行定时回调
    void run() const;

    // 重新计算下一次过期时间（仅对重复定时器有意义）
    void restart(TimePoint now);

    TimePoint expiration() const;
    bool repeat() const;

private:
    std::function<void()> callback_;
    TimePoint expiration_;
    double interval_;
    bool repeat_;
};

}  // namespace httpserver
