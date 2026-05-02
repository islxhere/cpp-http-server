#include "utils/logger.h"

#include <chrono>
#include <cstring>

namespace httpserver {

Logger::Logger()
    : level_(LogLevel::INFO),
      running_(false) {}

Logger::~Logger() {
    stop();
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::init(const std::string& logFileName, LogLevel level) {
    logFileName_ = logFileName;
    level_ = level;
    // 支持重复调用：先关闭旧流
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
    fileStream_.clear();  // 清除可能的错误状态
    fileStream_.open(logFileName, std::ios::app);
    running_ = true;
    backgroundThread_ = std::thread([this]() { threadFunc(); });
}

void Logger::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cond_.notify_one();
    if (backgroundThread_.joinable()) {
        backgroundThread_.join();
    }
    // 后台线程已退出，刷完可能残留的缓冲
    if (!buffer_.empty()) {
        for (auto& line : buffer_) {
            fileStream_ << line << '\n';
        }
        fileStream_.flush();
        buffer_.clear();
    }
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
}

void Logger::append(LogLevel level, const char* file, int line,
                    const std::string& msg) {
    if (level < level_) return;

    // 组装日志行：[LEVEL] timestamp file:line - msg
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf{};
    ::localtime_r(&timer, &tm_buf);

    char timebuf[64];
    std::snprintf(timebuf, sizeof(timebuf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                  static_cast<int>(ms.count()));

    // 只取文件名，不取完整路径
    const char* basename = std::strrchr(file, '/');
    basename = basename ? basename + 1 : file;

    std::string logLine = std::string("[") + levelToString(level) + "] "
                          + timebuf + " " + basename + ":" + std::to_string(line)
                          + " - " + msg;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.push_back(std::move(logLine));
    }
    cond_.notify_one();
}

void Logger::threadFunc() {
    std::vector<std::string> localBuffer;
    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait_for(lock, std::chrono::seconds(3), [this]() {
                return !buffer_.empty() || !running_;
            });
            buffer_.swap(localBuffer);
        }
        // 解锁后写文件，不持有锁
        for (auto& line : localBuffer) {
            fileStream_ << line << '\n';
        }
        fileStream_.flush();
        localBuffer.clear();

        if (!running_ && localBuffer.empty()) {
            // running_ 为 false 时，再交换一次把残余日志刷出
            std::lock_guard<std::mutex> lock(mutex_);
            buffer_.swap(localBuffer);
            for (auto& line : localBuffer) {
                fileStream_ << line << '\n';
            }
            fileStream_.flush();
            localBuffer.clear();
            break;
        }
    }
}

const char* Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default:              return "UNKN ";
    }
}

}  // namespace httpserver
