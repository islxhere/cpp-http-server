#include "core/channel.h"

#include "core/event_loop.h"

namespace httpserver {

Channel::Channel(EventLoop* loop, int fd)
    : event_loop_(loop),
      fd_(fd),
      events_(kNoneEvent),
      revents_(0) {}

Channel::~Channel() = default;

void Channel::handleEvent() {
    // EPOLLHUP 且没有可读事件时，触发 close 回调
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
        if (close_callback_) {
            close_callback_();
        }
    }

    // EPOLLERR 触发 error 回调
    if (revents_ & EPOLLERR) {
        if (error_callback_) {
            error_callback_();
        }
    }

    // 可读事件（EPOLLIN / EPOLLPRI / EPOLLRDHUP）
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
        if (read_callback_) {
            read_callback_();
        }
    }

    // 可写事件
    if (revents_ & EPOLLOUT) {
        if (write_callback_) {
            write_callback_();
        }
    }
}

void Channel::enableReading() {
    events_ |= kReadEvent;
    update();
}

void Channel::disableReading() {
    events_ &= ~kReadEvent;
    update();
}

void Channel::enableWriting() {
    events_ |= kWriteEvent;
    update();
}

void Channel::disableWriting() {
    events_ &= ~kWriteEvent;
    update();
}

void Channel::disableAll() {
    events_ = kNoneEvent;
    update();
}

bool Channel::isNoneEvent() const {
    return events_ == kNoneEvent;
}

int Channel::fd() const {
    return fd_;
}

uint32_t Channel::events() const {
    return events_;
}

uint32_t Channel::revents() const {
    return revents_;
}

void Channel::setRevents(uint32_t revents) {
    revents_ = revents;
}

EventLoop* Channel::ownerLoop() const {
    return event_loop_;
}

void Channel::setReadCallback(EventCallback cb) {
    read_callback_ = std::move(cb);
}

void Channel::setWriteCallback(EventCallback cb) {
    write_callback_ = std::move(cb);
}

void Channel::setCloseCallback(EventCallback cb) {
    close_callback_ = std::move(cb);
}

void Channel::setErrorCallback(EventCallback cb) {
    error_callback_ = std::move(cb);
}

void Channel::update() {
    if (event_loop_) {
        event_loop_->updateChannel(this);
    }
}

void Channel::remove() {
    if (event_loop_) {
        event_loop_->removeChannel(this);
    }
}

}  // namespace httpserver
