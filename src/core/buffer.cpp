#include "core/buffer.h"

#include <cerrno>
#include <cstring>
#include <sys/uio.h>
#include <unistd.h>

namespace httpserver {

Buffer::Buffer(size_t initial_size)
    : buffer_(kCheapPrepend + initial_size),
      reader_index_(kCheapPrepend),
      writer_index_(kCheapPrepend) {}

size_t Buffer::readableBytes() const {
    return writer_index_ - reader_index_;
}

size_t Buffer::writableBytes() const {
    return buffer_.size() - writer_index_;
}

size_t Buffer::prependableBytes() const {
    return reader_index_;
}

const char* Buffer::peek() const {
    return buffer_.data() + reader_index_;
}

void Buffer::retrieve(size_t len) {
    if (len < readableBytes()) {
        reader_index_ += len;
    } else {
        reader_index_ = writer_index_;
    }
}

void Buffer::retrieveUntil(const char* end) {
    retrieve(end - peek());
}

std::string Buffer::retrieveAllAsString() {
    return retrieveAsString(readableBytes());
}

std::string Buffer::retrieveAsString(size_t len) {
    std::string result(peek(), len);
    retrieve(len);
    return result;
}

void Buffer::ensureWritableBytes(size_t len) {
    if (writableBytes() < len) {
        // 扩容：把可读数据前移到预留位置，腾出更多可写空间
        if (prependableBytes() + writableBytes() >= len + kCheapPrepend) {
            // 内部空间够用，搬移数据
            size_t readable = readableBytes();
            std::memmove(buffer_.data() + kCheapPrepend,
                         buffer_.data() + reader_index_, readable);
            reader_index_ = kCheapPrepend;
            writer_index_ = reader_index_ + readable;
        } else {
            // 需要真正扩容
            buffer_.resize(writer_index_ + len);
        }
    }
}

void Buffer::append(const char* data, size_t len) {
    ensureWritableBytes(len);
    std::memcpy(beginWrite(), data, len);
    writer_index_ += len;
}

void Buffer::append(const std::string& str) {
    append(str.data(), str.size());
}

char* Buffer::beginWrite() {
    return buffer_.data() + writer_index_;
}

const char* Buffer::beginWrite() const {
    return buffer_.data() + writer_index_;
}

ssize_t Buffer::readFd(int fd, int* saved_errno) {
    // 使用栈上额外缓冲区，配合 readv 分散读
    // 避免频繁扩容 buffer_
    char extrabuf[65536];
    struct iovec vec[2];
    const size_t writable = writableBytes();

    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writable;
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);

    // 当 buffer_ 可写空间 < 64KB 时，readv 会同时使用两个缓冲区
    const ssize_t n = ::readv(fd, vec, 2);
    if (n < 0) {
        *saved_errno = errno;
    } else if (static_cast<size_t>(n) <= writable) {
        // 数据全部写入 buffer_
        writer_index_ += n;
    } else {
        // buffer_ 写满了，额外数据在 extrabuf 中
        writer_index_ = buffer_.size();
        append(extrabuf, n - writable);
    }
    return n;
}

}  // namespace httpserver
