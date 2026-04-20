#pragma once

// InetAddress — 封装 sockaddr_in，用于表示 IP 地址和端口号。
// 提供从 ip+port 构造和从 sockaddr_in 构造两种方式。

#include <arpa/inet.h>
#include <netinet/in.h>
#include <cstdint>
#include <string>

namespace httpserver {

class InetAddress {
public:
    // 通过 ip 和 port 构造，ip 为空时绑定 INADDR_ANY
    explicit InetAddress(uint16_t port, const std::string& ip = "0.0.0.0");

    // 从已有的 sockaddr_in 构造
    explicit InetAddress(const struct sockaddr_in& addr);

    // 默认构造（0.0.0.0:0）
    InetAddress();

    std::string ip() const;
    uint16_t port() const;
    std::string ipPort() const;

    const struct sockaddr_in& getSockAddr() const;
    void setSockAddr(const struct sockaddr_in& addr);

private:
    struct sockaddr_in addr_;
};

}  // namespace httpserver
