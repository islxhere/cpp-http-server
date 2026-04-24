#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <set>
#include <thread>

#include "core/event_loop.h"
#include "core/event_loop_thread_pool.h"

namespace httpserver {

// 测试单线程 fallback：不设置线程数，getNextLoop() 永远返回 baseLoop
TEST(EventLoopThreadPoolTest, SingleThreadFallback) {
    EventLoop base_loop;
    EventLoopThreadPool pool(&base_loop);

    pool.start();

    // 连续调用多次，都应该返回 baseLoop
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(pool.getNextLoop(), &base_loop);
    }
}

// 测试多线程轮询：3 个线程，5 次调用应按 [0,1,2,0,1] 分配
TEST(EventLoopThreadPoolTest, RoundRobin) {
    EventLoop base_loop;
    EventLoopThreadPool pool(&base_loop);

    pool.setThreadNum(3);
    pool.start();

    // 收集 5 次 getNextLoop() 的返回值
    EventLoop* loops[5];
    for (int i = 0; i < 5; ++i) {
        loops[i] = pool.getNextLoop();
    }

    // 验证轮询顺序：0→1→2→0→1
    EXPECT_EQ(loops[0], loops[3]);  // 第 1 次和第 4 次相同
    EXPECT_EQ(loops[1], loops[4]);  // 第 2 次和第 5 次相同

    // 三个子线程的 EventLoop 应该互不相同
    EXPECT_NE(loops[0], loops[1]);
    EXPECT_NE(loops[1], loops[2]);
    EXPECT_NE(loops[0], loops[2]);

    // 子线程的 EventLoop 不应该等于 baseLoop
    EXPECT_NE(loops[0], &base_loop);
    EXPECT_NE(loops[1], &base_loop);
    EXPECT_NE(loops[2], &base_loop);
}

// 测试子线程的 EventLoop 可以正常派发任务
TEST(EventLoopThreadPoolTest, SubLoopsCanDispatch) {
    EventLoop base_loop;
    EventLoopThreadPool pool(&base_loop);

    pool.setThreadNum(2);
    pool.start();

    std::atomic<int> counter{0};

    // 向两个子线程各派发一个任务
    EventLoop* loop0 = pool.getNextLoop();
    EventLoop* loop1 = pool.getNextLoop();

    loop0->queueInLoop([&counter]() { counter.fetch_add(1); });
    loop1->queueInLoop([&counter]() { counter.fetch_add(1); });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(counter.load(), 2);
}

}  // namespace httpserver
