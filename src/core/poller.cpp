#include "core/poller.h"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <unistd.h>

#include "core/channel.h"

namespace httpserver {

Poller::Poller()
    : epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(kInitEventListSize) {
    if (epollfd_ < 0) {
        throw std::runtime_error(std::string("epoll_create1 failed: ") + std::strerror(errno));
    }
}

Poller::~Poller() {
    ::close(epollfd_);
}

void Poller::poll(int timeout_ms, ChannelList* active_channels) {
    int num_events = ::epoll_wait(epollfd_, events_.data(),
                                  static_cast<int>(events_.size()), timeout_ms);
    if (num_events < 0) {
        if (errno == EINTR) {
            return;  // 被信号中断，视为正常
        }
        throw std::runtime_error(std::string("epoll_wait failed: ") + std::strerror(errno));
    }

    // 填充活跃 Channel
    for (int i = 0; i < num_events; ++i) {
        auto* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->setRevents(events_[i].events);
        active_channels->push_back(channel);
    }

    // 如果活跃事件数等于 events_ 容量，说明可能有更多事件待处理，扩容
    if (static_cast<size_t>(num_events) == events_.size()) {
        events_.resize(events_.size() * 2);
    }
}

void Poller::updateChannel(Channel* channel) {
    if (channel->isNoneEvent()) {
        update(EPOLL_CTL_DEL, channel);
        return;
    }

    struct epoll_event event{};
    event.events = channel->events();
    event.data.ptr = channel;

    if (::epoll_ctl(epollfd_, EPOLL_CTL_ADD, channel->fd(), &event) < 0) {
        if (errno == EEXIST) {
            ::epoll_ctl(epollfd_, EPOLL_CTL_MOD, channel->fd(), &event);
        } else {
            throw std::runtime_error(std::string("epoll_ctl failed: ") + std::strerror(errno));
        }
    }
}

void Poller::removeChannel(Channel* channel) {
    update(EPOLL_CTL_DEL, channel);
}

void Poller::update(int operation, Channel* channel) {
    struct epoll_event event{};
    event.events = channel->events();
    event.data.ptr = channel;

    if (::epoll_ctl(epollfd_, operation, channel->fd(), &event) < 0) {
        if (operation == EPOLL_CTL_DEL && errno == ENOENT) {
            return;  // fd 已经不在 epoll 中，忽略
        }
        throw std::runtime_error(std::string("epoll_ctl failed: ") + std::strerror(errno));
    }
}

}  // namespace httpserver
