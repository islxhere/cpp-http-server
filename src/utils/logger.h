#pragma once

// Logger — 异步日志系统（单例）。
// 前端通过 LOG_INFO 等宏流式写入，后台线程负责刷盘。
// 内部使用双缓冲（std::vector swap）减少锁持有时间。

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace httpserver {

class Logger {
public:
    enum class LogLevel { DEBUG, INFO, WARN, ERROR, FATAL };

    static Logger& instance();

    // 初始化日志文件，启动后台线程
    void init(const std::string& logFileName, LogLevel level = LogLevel::INFO);

    // 停止后台线程，刷完剩余日志
    void stop();

    // 前端写入接口（线程安全）
    void append(LogLevel level, const char* file, int line, const std::string& msg);

    void setLogLevel(LogLevel level) { level_ = level; }
    LogLevel logLevel() const { return level_; }

private:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void threadFunc();
    static const char* levelToString(LogLevel level);

    LogLevel level_;
    bool running_;
    std::string logFileName_;

    std::mutex mutex_;
    std::condition_variable cond_;
    std::vector<std::string> buffer_;  // 前端写入缓冲

    std::thread backgroundThread_;
    std::ofstream fileStream_;
};

// LogStream — 利用 RAII 在析构时将拼接好的字符串推入 Logger
class LogStream {
public:
    LogStream(Logger::LogLevel level, const char* file, int line)
        : level_(level), file_(file), line_(line) {}

    ~LogStream() {
        Logger::instance().append(level_, file_, line_, stream_);
    }

    // 支持流式写入
    LogStream& operator<<(const std::string& s) { stream_ += s; return *this; }
    LogStream& operator<<(const char* s) { stream_ += s; return *this; }
    LogStream& operator<<(int v) { stream_ += std::to_string(v); return *this; }
    LogStream& operator<<(long v) { stream_ += std::to_string(v); return *this; }
    LogStream& operator<<(long long v) { stream_ += std::to_string(v); return *this; }
    LogStream& operator<<(unsigned int v) { stream_ += std::to_string(v); return *this; }
    LogStream& operator<<(unsigned long v) { stream_ += std::to_string(v); return *this; }
    LogStream& operator<<(unsigned long long v) { stream_ += std::to_string(v); return *this; }
    LogStream& operator<<(double v) { stream_ += std::to_string(v); return *this; }
    LogStream& operator<<(char c) { stream_ += c; return *this; }

private:
    Logger::LogLevel level_;
    const char* file_;
    int line_;
    std::string stream_;
};

}  // namespace httpserver

#define LOG_DEBUG \
    ::httpserver::LogStream(::httpserver::Logger::LogLevel::DEBUG, __FILE__, __LINE__)
#define LOG_INFO \
    ::httpserver::LogStream(::httpserver::Logger::LogLevel::INFO, __FILE__, __LINE__)
#define LOG_WARN \
    ::httpserver::LogStream(::httpserver::Logger::LogLevel::WARN, __FILE__, __LINE__)
#define LOG_ERROR \
    ::httpserver::LogStream(::httpserver::Logger::LogLevel::ERROR, __FILE__, __LINE__)
#define LOG_FATAL \
    ::httpserver::LogStream(::httpserver::Logger::LogLevel::FATAL, __FILE__, __LINE__)
