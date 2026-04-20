#include "core/channel.h"

#include <gtest/gtest.h>

namespace httpserver {

// Channel 不拥有 fd，测试时用 pipe 创建的 fd 即可
class ChannelTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(::pipe(pipefd_), 0);
    }

    void TearDown() override {
        ::close(pipefd_[0]);
        ::close(pipefd_[1]);
    }

    int pipefd_[2];
};

TEST_F(ChannelTest, InitialState) {
    Channel ch(nullptr, pipefd_[0]);
    EXPECT_EQ(ch.fd(), pipefd_[0]);
    EXPECT_EQ(ch.events(), 0u);
    EXPECT_TRUE(ch.isNoneEvent());
    EXPECT_EQ(ch.ownerLoop(), nullptr);
}

TEST_F(ChannelTest, EnableDisableReading) {
    Channel ch(nullptr, pipefd_[0]);

    ch.enableReading();
    EXPECT_NE(ch.events() & EPOLLIN, 0u);
    EXPECT_FALSE(ch.isNoneEvent());

    ch.disableReading();
    EXPECT_EQ(ch.events() & EPOLLIN, 0u);
    EXPECT_TRUE(ch.isNoneEvent());
}

TEST_F(ChannelTest, EnableDisableWriting) {
    Channel ch(nullptr, pipefd_[0]);

    ch.enableWriting();
    EXPECT_NE(ch.events() & EPOLLOUT, 0u);
    EXPECT_FALSE(ch.isNoneEvent());

    ch.disableWriting();
    EXPECT_EQ(ch.events() & EPOLLOUT, 0u);
    EXPECT_TRUE(ch.isNoneEvent());
}

TEST_F(ChannelTest, DisableAll) {
    Channel ch(nullptr, pipefd_[0]);

    ch.enableReading();
    ch.enableWriting();
    EXPECT_FALSE(ch.isNoneEvent());

    ch.disableAll();
    EXPECT_TRUE(ch.isNoneEvent());
    EXPECT_EQ(ch.events(), 0u);
}

TEST_F(ChannelTest, ReadCallbackTriggered) {
    Channel ch(nullptr, pipefd_[0]);

    bool called = false;
    ch.setReadCallback([&called]() { called = true; });

    // 模拟 Poller 设置 revents
    ch.setRevents(EPOLLIN);
    ch.handleEvent();
    EXPECT_TRUE(called);
}

TEST_F(ChannelTest, WriteCallbackTriggered) {
    Channel ch(nullptr, pipefd_[0]);

    bool called = false;
    ch.setWriteCallback([&called]() { called = true; });

    ch.setRevents(EPOLLOUT);
    ch.handleEvent();
    EXPECT_TRUE(called);
}

TEST_F(ChannelTest, ErrorCallbackTriggered) {
    Channel ch(nullptr, pipefd_[0]);

    bool called = false;
    ch.setErrorCallback([&called]() { called = true; });

    ch.setRevents(EPOLLERR);
    ch.handleEvent();
    EXPECT_TRUE(called);
}

TEST_F(ChannelTest, CloseCallbackTriggered) {
    Channel ch(nullptr, pipefd_[0]);

    bool called = false;
    ch.setCloseCallback([&called]() { called = true; });

    // EPOLLHUP 且没有 EPOLLIN
    ch.setRevents(EPOLLHUP);
    ch.handleEvent();
    EXPECT_TRUE(called);
}

TEST_F(ChannelTest, NoCallbackWhenNoEvents) {
    Channel ch(nullptr, pipefd_[0]);

    bool read_called = false;
    bool write_called = false;
    ch.setReadCallback([&read_called]() { read_called = true; });
    ch.setWriteCallback([&write_called]() { write_called = true; });

    ch.setRevents(0);
    ch.handleEvent();
    EXPECT_FALSE(read_called);
    EXPECT_FALSE(write_called);
}

TEST_F(ChannelTest, MultipleCallbacks) {
    Channel ch(nullptr, pipefd_[0]);

    bool read_called = false;
    bool write_called = false;
    ch.setReadCallback([&read_called]() { read_called = true; });
    ch.setWriteCallback([&write_called]() { write_called = true; });

    // 同时可读可写
    ch.setRevents(EPOLLIN | EPOLLOUT);
    ch.handleEvent();
    EXPECT_TRUE(read_called);
    EXPECT_TRUE(write_called);
}

}  // namespace httpserver
