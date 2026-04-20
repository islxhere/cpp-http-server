#pragma once

// Poller — 封装 epoll，负责监听文件描述符上的 IO 事件。
// 通过 epoll_wait 获取活跃事件，填充 activeChannels 供 EventLoop 分发。

#include <sys/epoll.h>

#include <vector>

namespace httpserver {

class Channel;

class Poller {
public:
    using ChannelList = std::vector<Channel*>;

    Poller();
    ~Poller();

    // 禁止拷贝
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;

    // 等待 IO 事件，将活跃的 Channel 填入 activeChannels
    void poll(int timeout_ms, ChannelList* active_channels);

    // 添加或修改 Channel 的监听事件
    void updateChannel(Channel* channel);

    // 移除 Channel 的监听
    void removeChannel(Channel* channel);

private:
    void update(int operation, Channel* channel);

    int epollfd_;
    std::vector<struct epoll_event> events_;

    static constexpr int kInitEventListSize = 16;
};

}  // namespace httpserver
