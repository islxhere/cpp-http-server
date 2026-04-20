#include "core/inet_address.h"

#include <cstring>

namespace httpserver {

InetAddress::InetAddress(uint16_t port, const std::string& ip) {
    std::memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
    addr_.sin_port = htons(port);
    ::inet_pton(AF_INET, ip.c_str(), &addr_.sin_addr);
}

InetAddress::InetAddress(const struct sockaddr_in& addr) : addr_(addr) {}

InetAddress::InetAddress() {
    std::memset(&addr_, 0, sizeof(addr_));
    addr_.sin_family = AF_INET;
}

std::string InetAddress::ip() const {
    char buf[64] = {0};
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof(buf));
    return buf;
}

uint16_t InetAddress::port() const {
    return ntohs(addr_.sin_port);
}

std::string InetAddress::ipPort() const {
    return ip() + ":" + std::to_string(port());
}

const struct sockaddr_in& InetAddress::getSockAddr() const {
    return addr_;
}

void InetAddress::setSockAddr(const struct sockaddr_in& addr) {
    addr_ = addr;
}

}  // namespace httpserver
