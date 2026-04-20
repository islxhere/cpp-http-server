#include "core/event_loop.h"

#include <sys/timerfd.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "core/channel.h"
#include "gtest/gtest.h"

namespace httpserver {

// 使用 timerfd 验证 EventLoop 的事件循环：
// 创建 1 秒定时器 → 注册到 EventLoop → loop() → readCallback 触发 → quit()
TEST(EventLoopTest, TimerFdTriggersQuit) {
    // 1. 创建 timerfd，1 秒后触发一次
    int timer_fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    ASSERT_GE(timer_fd, 0);

    struct itimerspec spec{};
    spec.it_value.tv_sec = 1;  // 1 秒后触发
    ASSERT_EQ(::timerfd_settime(timer_fd, 0, &spec, nullptr), 0);

    // 2. 创建 EventLoop
    EventLoop loop;

    // 3. 创建 Channel，绑定 timer_fd
    Channel channel(&loop, timer_fd);
    std::atomic<bool> fired{false};

    channel.setReadCallback([&]() {
        // 读取 timerfd 的超时通知（必须读，否则 epoll 会持续触发）
        uint64_t exp;
        ::read(timer_fd, &exp, sizeof(exp));

        fired = true;
        loop.quit();  // 退出事件循环
    });

    // 4. 注册可读事件并启动循环
    channel.enableReading();

    auto start = std::chrono::steady_clock::now();
    loop.loop();
    auto end = std::chrono::steady_clock::now();

    // 5. 验证
    EXPECT_TRUE(fired);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(elapsed.count(), 900);   // 至少等了约 1 秒
    EXPECT_LE(elapsed.count(), 2000);  // 不应超过 2 秒

    // 清理
    channel.disableAll();
    channel.remove();
    ::close(timer_fd);
}

}  // namespace httpserver
