#include "core/buffer.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace httpserver {

TEST(BufferTest, InitialState) {
    Buffer buf;
    EXPECT_EQ(buf.readableBytes(), 0u);
    EXPECT_EQ(buf.writableBytes(), Buffer::kInitialSize);
    EXPECT_EQ(buf.prependableBytes(), Buffer::kCheapPrepend);
}

TEST(BufferTest, AppendAndRetrieve) {
    Buffer buf;
    std::string data = "hello";
    buf.append(data);

    EXPECT_EQ(buf.readableBytes(), 5u);
    EXPECT_EQ(std::string(buf.peek(), buf.readableBytes()), "hello");

    std::string result = buf.retrieveAllAsString();
    EXPECT_EQ(result, "hello");
    EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(BufferTest, AppendCharData) {
    Buffer buf;
    const char data[] = "world";
    buf.append(data, 5);

    EXPECT_EQ(buf.readableBytes(), 5u);
    EXPECT_EQ(std::string(buf.peek(), buf.readableBytes()), "world");
}

TEST(BufferTest, RetrievePartial) {
    Buffer buf;
    buf.append("abcdef", 6);

    std::string part = buf.retrieveAsString(3);
    EXPECT_EQ(part, "abc");
    EXPECT_EQ(buf.readableBytes(), 3u);
    EXPECT_EQ(std::string(buf.peek(), buf.readableBytes()), "def");

    buf.retrieve(1);
    EXPECT_EQ(buf.readableBytes(), 2u);
    EXPECT_EQ(std::string(buf.peek(), buf.readableBytes()), "ef");
}

TEST(BufferTest, RetrieveUntil) {
    Buffer buf;
    buf.append("hello world", 11);

    const char* end = buf.peek() + 5;
    buf.retrieveUntil(end);
    EXPECT_EQ(buf.readableBytes(), 6u);
    EXPECT_EQ(std::string(buf.peek(), buf.readableBytes()), " world");
}

TEST(BufferTest, EnsureWritableBytes) {
    Buffer buf(16);
    EXPECT_EQ(buf.writableBytes(), 16u);

    // 写满 16 字节
    buf.append("0123456789abcdef", 16);
    EXPECT_EQ(buf.writableBytes(), 0u);

    // ensureWritableBytes 应该扩容
    buf.ensureWritableBytes(64);
    EXPECT_GE(buf.writableBytes(), 64u);

    // 之前的数据应该还在
    EXPECT_EQ(buf.readableBytes(), 16u);
    EXPECT_EQ(std::string(buf.peek(), 16), "0123456789abcdef");
}

TEST(BufferTest, MultipleAppends) {
    Buffer buf;
    buf.append("one", 3);
    buf.append("two", 3);
    buf.append("three", 5);

    EXPECT_EQ(buf.readableBytes(), 11u);
    EXPECT_EQ(buf.retrieveAllAsString(), "onetwothree");
}

TEST(BufferTest, ReadFd) {
    // 创建 pipe 来测试 readFd
    int pipefd[2];
    ASSERT_EQ(::pipe(pipefd), 0);

    const char* msg = "hello from pipe";
    ::write(pipefd[1], msg, std::strlen(msg));
    ::close(pipefd[1]);

    Buffer buf;
    int saved_errno = 0;
    ssize_t n = buf.readFd(pipefd[0], &saved_errno);
    EXPECT_GT(n, 0);
    EXPECT_EQ(static_cast<size_t>(n), std::strlen(msg));
    EXPECT_EQ(buf.retrieveAllAsString(), "hello from pipe");

    ::close(pipefd[0]);
}

TEST(BufferTest, ReadFdLargeData) {
    // 测试 readv 的分散读能力（数据量超过 buffer 可写空间）
    int pipefd[2];
    ASSERT_EQ(::pipe(pipefd), 0);

    // 生成大于 kInitialSize 的数据
    std::string large_data(Buffer::kInitialSize + 1000, 'X');
    ::write(pipefd[1], large_data.data(), large_data.size());
    ::close(pipefd[1]);

    Buffer buf;
    int saved_errno = 0;
    ssize_t n = buf.readFd(pipefd[0], &saved_errno);
    EXPECT_EQ(static_cast<size_t>(n), large_data.size());
    EXPECT_EQ(buf.retrieveAllAsString(), large_data);

    ::close(pipefd[0]);
}

TEST(BufferTest, BeginWrite) {
    Buffer buf;
    char* ptr = buf.beginWrite();
    EXPECT_NE(ptr, nullptr);

    // 直接写入
    std::memcpy(ptr, "abc", 3);
    EXPECT_EQ(buf.readableBytes(), 0u);  // writer_index_ 还没移动

    // 通过 append 移动 writer_index_
    buf.append("abc", 3);
    EXPECT_EQ(buf.readableBytes(), 3u);
}

TEST(BufferTest, PrependableBytes) {
    Buffer buf;
    EXPECT_EQ(buf.prependableBytes(), Buffer::kCheapPrepend);

    buf.append("test", 4);
    EXPECT_EQ(buf.prependableBytes(), Buffer::kCheapPrepend);

    buf.retrieve(2);
    EXPECT_EQ(buf.prependableBytes(), Buffer::kCheapPrepend + 2);
}

}  // namespace httpserver
