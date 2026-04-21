#include <cstdio>

#include "core/event_loop.h"
#include "core/inet_address.h"
#include "http/http_request.h"
#include "http/http_response.h"
#include "http/http_server.h"

int main() {
    std::printf("HTTP Server starting on port 8080...\n");

    httpserver::EventLoop loop;
    httpserver::InetAddress listen_addr(8080);
    httpserver::HttpServer server(&loop, listen_addr);

    server.setHttpCallback(
        [](const httpserver::HttpRequest& req, httpserver::HttpResponse* resp) {
            if (req.method() == httpserver::HttpRequest::Method::kGet &&
                req.path() == "/") {
                resp->setStatusCode(httpserver::HttpResponse::HttpStatusCode::k200Ok);
                resp->setStatusMessage("OK");
                resp->setContentType("text/html");
                resp->setBody("<h1>Hello from my C++ Http Server!</h1>");
            } else {
                resp->setStatusCode(httpserver::HttpResponse::HttpStatusCode::k404NotFound);
                resp->setStatusMessage("Not Found");
                resp->setContentType("text/html");
                resp->setBody("<h1>404 Not Found</h1>");
            }
        });

    server.start();
    std::printf("HTTP Server is running on http://127.0.0.1:8080\n");
    loop.loop();

    return 0;
}
