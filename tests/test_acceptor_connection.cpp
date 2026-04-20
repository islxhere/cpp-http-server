#include <gtest/gtest.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <future>
#include <thread>

#include "core/acceptor.h"
#include "core/event_loop.h"
#include "core/inet_address.h"
#include "core/tcp_connection.h"

namespace httpserver {

// 简单的 Echo 测试：Acceptor 监听，客户端连接并发送数据，TcpConnection 回显。
TEST(AcceptorConnectionTest, EchoBasic) {
    // 先 bind 到随机端口获取端口号
    int tmp_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    ASSERT_GE(tmp_fd, 0);

    int reuse = 1;
    ::setsockopt(tmp_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind_addr.sin_port = 0;
    ASSERT_EQ(::bind(tmp_fd, reinterpret_cast<struct sockaddr*>(&bind_addr),
                     sizeof(bind_addr)),
              0);

    struct sockaddr_in actual_addr{};
    socklen_t addr_len = sizeof(actual_addr);
    ::getsockname(tmp_fd, reinterpret_cast<struct sockaddr*>(&actual_addr), &addr_len);
    uint16_t port = ntohs(actual_addr.sin_port);
    ::close(tmp_fd);

    InetAddress listen_addr(port, "127.0.0.1");

    // 存储已建立的连接，防止 shared_ptr 提前释放
    std::vector<std::shared_ptr<TcpConnection>> connections;
    std::atomic<bool> loop_ready{false};

    // 在子线程中创建 EventLoop + Acceptor 并运行
    std::thread loop_thread([&]() {
        EventLoop loop;

        Acceptor acceptor(&loop, listen_addr);

        acceptor.setNewConnectionCallback([&](int conn_fd, const InetAddress& peer_addr) {
            auto conn = std::make_shared<TcpConnection>(&loop, conn_fd, listen_addr, peer_addr);
            conn->setMessageCallback(
                [](const std::shared_ptr<TcpConnection>& c, Buffer* buf) {
                    std::string data = buf->retrieveAllAsString();
                    c->send(data);
                });
            conn->setCloseCallback([&](const std::shared_ptr<TcpConnection>& c) {
                c->connectDestroyed();
                for (auto it = connections.begin(); it != connections.end(); ++it) {
                    if (it->get() == c.get()) {
                        connections.erase(it);
                        break;
                    }
                }
            });
            conn->connectEstablished();
            connections.push_back(conn);
        });

        acceptor.listen();
        loop_ready = true;
        loop.loop();
    });

    // 等待 loop 启动
    while (!loop_ready) {
        usleep(1000);
    }
    usleep(10000);

    // 客户端连接
    int client_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    ASSERT_GE(client_fd, 0);

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(port);

    int ret = ::connect(client_fd, reinterpret_cast<struct sockaddr*>(&server_addr),
                        sizeof(server_addr));
    if (ret < 0 && errno != EINPROGRESS) {
        FAIL() << "connect failed: " << std::strerror(errno);
    }

    // 等待连接建立
    usleep(50000);

    // 发送数据
    const char* test_data = "hello world";
    ::write(client_fd, test_data, strlen(test_data));

    // 等待 echo
    usleep(50000);

    // 读取回显
    char buf[256] = {};
    ssize_t n = ::read(client_fd, buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_EQ(std::string(buf, n), "hello world");

    // 关闭客户端
    ::close(client_fd);

    // 等待服务器处理关闭事件
    usleep(50000);

    // detach 线程，测试目的已达到（验证 echo 功能）
    // EventLoop 的 quit 机制在后续 TcpServer 中统一处理
    loop_thread.detach();
}

}  // namespace httpserver
