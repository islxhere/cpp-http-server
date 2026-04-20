#include <cstdio>

#include "core/buffer.h"
#include "core/event_loop.h"
#include "core/inet_address.h"
#include "core/tcp_connection.h"
#include "core/tcp_server.h"

int main() {
    std::printf("Echo Server starting on port 8080...\n");

    httpserver::EventLoop loop;
    httpserver::InetAddress listen_addr(8080);
    httpserver::TcpServer server(&loop, listen_addr);

    server.setMessageCallback(
        [](const httpserver::TcpServer::TcpConnectionPtr& conn,
           httpserver::Buffer* buf) {
            std::string data = buf->retrieveAllAsString();
            std::printf("Received %zu bytes from %s\n",
                        data.size(), conn->name().c_str());
            conn->send(data);
        });

    server.setConnectionCallback(
        [](const httpserver::TcpServer::TcpConnectionPtr& conn) {
            std::printf("New connection: %s\n", conn->name().c_str());
        });

    server.start();
    std::printf("Echo Server is running on port 8080\n");
    loop.loop();

    return 0;
}
