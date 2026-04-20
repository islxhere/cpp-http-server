#pragma once

// Buffer — 应用层读写缓冲区，参考 muduo 的设计。
// 内部使用 vector<char>，分为 prependable、readable、writable 三个区域。
//
// +-------------------+------------------+------------------+
// | prependable bytes |  readable bytes  |  writable bytes  |
// |                   |     (CONTENT)    |                  |
// +-------------------+------------------+------------------+
// |                   |                  |                  |
// 0      <=      readerIndex   <=   writerIndex    <=     size()

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace httpserver {

class Buffer {
public:
    static constexpr size_t kCheapPrepend = 8;   // 预留空间大小
    static constexpr size_t kInitialSize = 1024; // 初始缓冲区大小

    explicit Buffer(size_t initial_size = kInitialSize);

    // 可读字节数
    size_t readableBytes() const;

    // 可写字节数
    size_t writableBytes() const;

    // 前方可预留字节数
    size_t prependableBytes() const;

    // 返回可读数据的起始位置
    const char* peek() const;

    // 读取 len 字节，移动读指针
    void retrieve(size_t len);

    // 读取到 end 指针位置
    void retrieveUntil(const char* end);

    // 读取所有数据并返回 string
    std::string retrieveAllAsString();

    // 读取 len 字节并返回 string
    std::string retrieveAsString(size_t len);

    // 确保可写区域至少有 len 字节，不足则扩容
    void ensureWritableBytes(size_t len);

    // 向可写区域追加数据
    void append(const char* data, size_t len);
    void append(const std::string& str);

    // 返回可写区域的起始位置
    char* beginWrite();
    const char* beginWrite() const;

    // 从 fd 读取数据到 Buffer，返回读取的字节数，出错返回 -1
    ssize_t readFd(int fd, int* saved_errno);

private:
    std::vector<char> buffer_;
    size_t reader_index_;
    size_t writer_index_;
};

}  // namespace httpserver
