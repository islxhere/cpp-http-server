#include <string>

#include <sys/types.h>
#include <unistd.h>

#include "core/event_loop.h"
#include "core/inet_address.h"
#include "http/http_request.h"
#include "http/http_response.h"
#include "http/http_server.h"
#include "utils/logger.h"

int main() {
    httpserver::Logger::instance().init("/tmp/httpserver.log");
    LOG_INFO << "HTTP Server starting on port 8080...";

    httpserver::EventLoop loop;
    httpserver::InetAddress listen_addr(8080);
    httpserver::HttpServer server(&loop, listen_addr);

    server.setThreadNum(4);

    server.setHttpCallback(
        [](const httpserver::HttpRequest& req, httpserver::HttpResponse* resp) {
            if (req.method() == httpserver::HttpRequest::Method::kGet &&
                req.path() == "/") {
                pid_t tid = ::gettid();
                std::string body = "<h1>Hello! Handled by Thread: " +
                                   std::to_string(tid) + "</h1>";
                resp->setStatusCode(httpserver::HttpResponse::HttpStatusCode::k200Ok);
                resp->setStatusMessage("OK");
                resp->setContentType("text/html");
                resp->setBody(body);
            } else {
                resp->setStatusCode(httpserver::HttpResponse::HttpStatusCode::k404NotFound);
                resp->setStatusMessage("Not Found");
                resp->setContentType("text/html");
                resp->setBody("<h1>404 Not Found</h1>");
            }
        });

    server.start();
    LOG_INFO << "HTTP Server is running on http://127.0.0.1:8080 with 4 worker threads";
    loop.loop();

    httpserver::Logger::instance().stop();
    return 0;
}
