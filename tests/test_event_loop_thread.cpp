#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "core/event_loop.h"
#include "core/event_loop_thread.h"

namespace httpserver {

TEST(EventLoopThreadTest, StartLoopReturnsNonNull) {
    EventLoopThread elt;
    EventLoop* loop = elt.startLoop();
    EXPECT_NE(loop, nullptr);
}

TEST(EventLoopThreadTest, QueueInLoopExecutesInSubThread) {
    EventLoopThread elt;
    EventLoop* loop = elt.startLoop();

    std::atomic<int> value{0};

    // 往子线程派发任务：修改原子变量
    loop->queueInLoop([&value]() { value.store(42); });

    // 等待子线程执行
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(value.load(), 42);
}

TEST(EventLoopThreadTest, MultipleTasksExecuted) {
    EventLoopThread elt;
    EventLoop* loop = elt.startLoop();

    std::atomic<int> counter{0};

    // 派发多个任务
    for (int i = 0; i < 5; ++i) {
        loop->queueInLoop([&counter]() { counter.fetch_add(1); });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(counter.load(), 5);
}

TEST(EventLoopThreadTest, LoopRunsInDifferentThread) {
    EventLoopThread elt;
    EventLoop* loop = elt.startLoop();

    std::atomic<std::thread::id> sub_thread_id{};

    loop->queueInLoop([&sub_thread_id]() {
        sub_thread_id.store(std::this_thread::get_id());
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 子线程的线程 ID 应该与主线程不同
    EXPECT_NE(sub_thread_id.load(), std::this_thread::get_id());
}

}  // namespace httpserver
