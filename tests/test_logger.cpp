#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "utils/logger.h"

namespace httpserver {

TEST(LoggerTest, MultiThreadWrite) {
    const std::string logFile = "/tmp/test_logger_mt.log";
    std::remove(logFile.c_str());

    constexpr int kThreads = 4;
    constexpr int kMessagesPerThread = 2500;
    constexpr int kTotal = kThreads * kMessagesPerThread;

    auto& logger = Logger::instance();
    logger.setLogLevel(Logger::LogLevel::DEBUG);
    logger.init(logFile, Logger::LogLevel::DEBUG);

    std::atomic<int> counter{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&counter, t]() {
            for (int i = 0; i < kMessagesPerThread; ++i) {
                int seq = counter.fetch_add(1, std::memory_order_relaxed);
                LOG_INFO << "thread=" << t << " seq=" << seq;
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // 停止 Logger，等待后台线程刷完
    logger.stop();

    // 统计日志文件行数
    std::ifstream ifs(logFile);
    ASSERT_TRUE(ifs.is_open());
    int lineCount = 0;
    std::string line;
    while (std::getline(ifs, line)) {
        ++lineCount;
    }
    ifs.close();

    EXPECT_EQ(lineCount, kTotal);

    // 清理
    std::remove(logFile.c_str());
}

TEST(LoggerTest, LogLevelFilter) {
    const std::string logFile = "/tmp/test_logger_level.log";
    std::remove(logFile.c_str());

    auto& logger = Logger::instance();
    logger.init(logFile, Logger::LogLevel::WARN);

    // 这些应该被过滤掉
    LOG_DEBUG << "this should not appear";
    LOG_INFO << "this should not appear either";

    // 这些应该被记录
    LOG_WARN << "warning message";
    LOG_ERROR << "error message";

    logger.stop();

    std::ifstream ifs(logFile);
    ASSERT_TRUE(ifs.is_open());
    int lineCount = 0;
    std::string line;
    while (std::getline(ifs, line)) {
        ++lineCount;
    }
    ifs.close();

    EXPECT_EQ(lineCount, 2);

    std::remove(logFile.c_str());
}

TEST(LoggerTest, InitAndStop) {
    const std::string logFile = "/tmp/test_logger_lifecycle.log";
    std::remove(logFile.c_str());

    auto& logger = Logger::instance();
    logger.init(logFile, Logger::LogLevel::INFO);

    LOG_INFO << "hello";
    LOG_INFO << "world";

    logger.stop();

    std::ifstream ifs(logFile);
    ASSERT_TRUE(ifs.is_open());
    int lineCount = 0;
    std::string line;
    while (std::getline(ifs, line)) {
        ++lineCount;
    }
    ifs.close();

    EXPECT_EQ(lineCount, 2);

    std::remove(logFile.c_str());
}

}  // namespace httpserver
