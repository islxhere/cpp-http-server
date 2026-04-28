#include <gtest/gtest.h>

#include <atomic>
#include <chrono>

#include "core/event_loop.h"
#include "utils/timer.h"
#include "utils/timer_queue.h"

namespace httpserver {

TEST(TimerQueueTest, OneShotAndRepeatingTimer) {
    EventLoop loop;
    TimerQueue timer_queue(&loop);

    std::atomic<int> counter_a{0};
    std::atomic<int> counter_b{0};

    auto now = Timer::Clock::now();

    // 一次性定时器：1 秒后触发
    auto* timer_a = new Timer(
        [&counter_a]() { counter_a.fetch_add(1); },
        now + std::chrono::seconds(1));

    // 重复定时器：每 0.5 秒触发
    auto* timer_b = new Timer(
        [&counter_b]() { counter_b.fetch_add(1); },
        now + std::chrono::milliseconds(500),
        0.5);

    // 退出定时器：2.2 秒后退出 loop
    auto* timer_quit = new Timer(
        [&loop]() { loop.quit(); },
        now + std::chrono::milliseconds(2200));

    timer_queue.addTimer(timer_a);
    timer_queue.addTimer(timer_b);
    timer_queue.addTimer(timer_quit);

    loop.loop();

    // 一次性定时器应触发 1 次
    EXPECT_EQ(counter_a.load(), 1);

    // 重复定时器在 2.2 秒内应在 0.5, 1.0, 1.5, 2.0 各触发一次 = 4 次
    EXPECT_EQ(counter_b.load(), 4);
}

TEST(TimerQueueTest, MultipleOneShotTimers) {
    EventLoop loop;
    TimerQueue timer_queue(&loop);

    std::atomic<int> counter{0};
    auto now = Timer::Clock::now();

    // 添加 3 个一次性定时器，间隔 100ms
    for (int i = 0; i < 3; ++i) {
        auto* t = new Timer(
            [&counter]() { counter.fetch_add(1); },
            now + std::chrono::milliseconds(100 * (i + 1)));
        timer_queue.addTimer(t);
    }

    // 500ms 后退出
    auto* quit_timer = new Timer(
        [&loop]() { loop.quit(); },
        now + std::chrono::milliseconds(500));
    timer_queue.addTimer(quit_timer);

    loop.loop();

    EXPECT_EQ(counter.load(), 3);
}

}  // namespace httpserver
