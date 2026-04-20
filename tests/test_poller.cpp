#include "core/poller.h"

#include <gtest/gtest.h>

#include <unistd.h>

#include "core/channel.h"

namespace httpserver {

class PollerTest : public ::testing::Test {
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

TEST_F(PollerTest, PollWithNoChannels) {
    Poller poller;
    Poller::ChannelList active_channels;
    // timeout=0，立即返回，无活跃 Channel
    poller.poll(0, &active_channels);
    EXPECT_TRUE(active_channels.empty());
}

TEST_F(PollerTest, PipeReadEvent) {
    Poller poller;
    Channel channel(nullptr, pipefd_[0]);
    channel.enableReading();

    // 注册到 Poller
    poller.updateChannel(&channel);

    // 写入数据使 pipe 可读
    const char data[] = "hello";
    ::write(pipefd_[1], data, sizeof(data));

    Poller::ChannelList active_channels;
    poller.poll(100, &active_channels);

    ASSERT_EQ(active_channels.size(), 1u);
    EXPECT_EQ(active_channels[0]->fd(), pipefd_[0]);
    EXPECT_NE(active_channels[0]->revents() & EPOLLIN, 0u);
}

TEST_F(PollerTest, PipeReadCallbackFired) {
    Poller poller;
    Channel channel(nullptr, pipefd_[0]);
    channel.enableReading();

    bool read_called = false;
    channel.setReadCallback([&read_called]() { read_called = true; });

    poller.updateChannel(&channel);

    // 写入数据
    ::write(pipefd_[1], "x", 1);

    Poller::ChannelList active_channels;
    poller.poll(100, &active_channels);

    ASSERT_EQ(active_channels.size(), 1u);
    active_channels[0]->handleEvent();
    EXPECT_TRUE(read_called);
}

TEST_F(PollerTest, NoEventWhenPipeEmpty) {
    Poller poller;
    Channel channel(nullptr, pipefd_[0]);
    channel.enableReading();

    poller.updateChannel(&channel);

    // 不写入数据，pipe 不可读
    Poller::ChannelList active_channels;
    poller.poll(0, &active_channels);

    EXPECT_TRUE(active_channels.empty());
}

TEST_F(PollerTest, RemoveChannel) {
    Poller poller;
    Channel channel(nullptr, pipefd_[0]);
    channel.enableReading();

    poller.updateChannel(&channel);
    poller.removeChannel(&channel);

    // 写入数据
    ::write(pipefd_[1], "x", 1);

    Poller::ChannelList active_channels;
    poller.poll(0, &active_channels);

    EXPECT_TRUE(active_channels.empty());
}

TEST_F(PollerTest, UpdateChannelEvents) {
    Poller poller;
    Channel channel(nullptr, pipefd_[0]);
    channel.enableReading();

    poller.updateChannel(&channel);

    // 先写入数据
    ::write(pipefd_[1], "x", 1);

    Poller::ChannelList active_channels;
    poller.poll(100, &active_channels);
    ASSERT_EQ(active_channels.size(), 1u);

    // 读取 pipe 中的数据，清空可读状态
    char buf[1];
    ::read(pipefd_[0], buf, 1);

    // 现在 pipe 不可读了
    active_channels.clear();
    poller.poll(0, &active_channels);
    EXPECT_TRUE(active_channels.empty());
}

TEST_F(PollerTest, MultipleChannels) {
    // 创建第二对 pipe
    int pipe2[2];
    ASSERT_EQ(::pipe(pipe2), 0);

    Poller poller;
    Channel ch1(nullptr, pipefd_[0]);
    Channel ch2(nullptr, pipe2[0]);

    ch1.enableReading();
    ch2.enableReading();

    poller.updateChannel(&ch1);
    poller.updateChannel(&ch2);

    // 只往第一个 pipe 写数据
    ::write(pipefd_[1], "a", 1);

    Poller::ChannelList active_channels;
    poller.poll(100, &active_channels);

    ASSERT_EQ(active_channels.size(), 1u);
    EXPECT_EQ(active_channels[0]->fd(), pipefd_[0]);

    ::close(pipe2[0]);
    ::close(pipe2[1]);
}

}  // namespace httpserver
