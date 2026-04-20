#include "core/inet_address.h"

#include <gtest/gtest.h>

#include <cstring>
#include <netinet/in.h>

namespace httpserver {

TEST(InetAddressTest, DefaultConstructor) {
    InetAddress addr;
    EXPECT_EQ(addr.ip(), "0.0.0.0");
    EXPECT_EQ(addr.port(), 0u);
}

TEST(InetAddressTest, PortConstructor) {
    InetAddress addr(8080);
    EXPECT_EQ(addr.ip(), "0.0.0.0");
    EXPECT_EQ(addr.port(), 8080u);
}

TEST(InetAddressTest, IpPortConstructor) {
    InetAddress addr(8080, "127.0.0.1");
    EXPECT_EQ(addr.ip(), "127.0.0.1");
    EXPECT_EQ(addr.port(), 8080u);
    EXPECT_EQ(addr.ipPort(), "127.0.0.1:8080");
}

TEST(InetAddressTest, SockaddrConstructor) {
    struct sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9090);
    ::inet_pton(AF_INET, "10.0.0.1", &sa.sin_addr);

    InetAddress addr(sa);
    EXPECT_EQ(addr.ip(), "10.0.0.1");
    EXPECT_EQ(addr.port(), 9090u);
}

TEST(InetAddressTest, GetSetSockAddr) {
    InetAddress addr(3000, "192.168.1.1");
    const auto& sa = addr.getSockAddr();
    EXPECT_EQ(sa.sin_family, AF_INET);
    EXPECT_EQ(ntohs(sa.sin_port), 3000u);

    // setSockAddr
    struct sockaddr_in sa2;
    std::memset(&sa2, 0, sizeof(sa2));
    sa2.sin_family = AF_INET;
    sa2.sin_port = htons(5000);
    ::inet_pton(AF_INET, "172.16.0.1", &sa2.sin_addr);

    addr.setSockAddr(sa2);
    EXPECT_EQ(addr.ip(), "172.16.0.1");
    EXPECT_EQ(addr.port(), 5000u);
}

TEST(InetAddressTest, IpPortString) {
    InetAddress addr(65535, "255.255.255.255");
    EXPECT_EQ(addr.ipPort(), "255.255.255.255:65535");
}

}  // namespace httpserver
