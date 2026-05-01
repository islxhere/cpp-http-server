#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "core/event_loop.h"
#include "core/tcp_connection.h"
#include "core/tcp_server.h"
#include "core/inet_address.h"

namespace httpserver {

// 辅助：创建 TCP 客户端连接到指定端口，失败返回 -1
static int connectTo(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

TEST(IdleConnectionTest, IdleClientDisconnected) {
    EventLoop loop;
    InetAddress listen_addr(12349);
    TcpServer server(&loop, listen_addr);

    std::atomic<int> conn_count{0};
    std::atomic<bool> disconnected{false};

    server.setConnectionCallback([&](const TcpConnection::TcpConnectionPtr&) {
        conn_count.fetch_add(1);
    });

    server.setMessageCallback(
        [](const TcpConnection::TcpConnectionPtr&, Buffer*) {});

    server.setIdleTimeout(2000);
    server.start();

    // 客户端线程
    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        int fd = connectTo(12349);
        if (fd < 0) return;

        // 不发送任何数据，等待超过空闲超时
        std::this_thread::sleep_for(std::chrono::milliseconds(3500));

        // 服务器应已关闭连接
        char buf[64];
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n == 0) {
            disconnected.store(true);
        }

        ::close(fd);
    });

    // 5 秒后退出 loop
    loop.runAfter(5.0, [&loop]() { loop.quit(); });
    loop.loop();

    client_thread.join();
    EXPECT_EQ(conn_count.load(), 1);
    EXPECT_TRUE(disconnected.load());
}

TEST(IdleConnectionTest, ActiveClientNotDisconnected) {
    EventLoop loop;
    InetAddress listen_addr(12350);
    TcpServer server(&loop, listen_addr);

    std::atomic<int> conn_count{0};
    std::atomic<bool> still_alive{true};

    server.setConnectionCallback([&](const TcpConnection::TcpConnectionPtr&) {
        conn_count.fetch_add(1);
    });

    server.setMessageCallback(
        [](const TcpConnection::TcpConnectionPtr& conn, Buffer* buf) {
            std::string data(buf->retrieveAllAsString());
            conn->send(data);
        });

    server.setIdleTimeout(2000);
    server.start();

    // 客户端线程：每秒发送数据保持活跃
    std::thread client_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        int fd = connectTo(12350);
        if (fd < 0) {
            still_alive.store(false);
            return;
        }

        for (int i = 0; i < 4; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            const char* msg = "ping";
            ssize_t n = ::send(fd, msg, strlen(msg), 0);
            if (n <= 0) {
                still_alive.store(false);
                break;
            }
        }

        // 读取 echo 回来的数据（非阻塞）
        char buf[64];
        ssize_t n = ::recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n == 0) {
            still_alive.store(false);  // 被服务器关闭了
        }

        // 主动关闭连接，触发服务器端清理
        ::shutdown(fd, SHUT_WR);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ::close(fd);
    });

    // 6 秒后退出 loop（给服务器时间处理连接关闭）
    loop.runAfter(6.0, [&loop]() { loop.quit(); });
    loop.loop();

    client_thread.join();
    EXPECT_EQ(conn_count.load(), 1);
    EXPECT_TRUE(still_alive.load());
}

}  // namespace httpserver
