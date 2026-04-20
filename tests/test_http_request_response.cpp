#include <gtest/gtest.h>

#include "core/buffer.h"
#include "http/http_request.h"
#include "http/http_response.h"

namespace httpserver {

// ==================== HttpRequest 测试 ====================

TEST(HttpRequestTest, DefaultState) {
    HttpRequest req;
    EXPECT_EQ(req.method(), HttpRequest::Method::kInvalid);
    EXPECT_EQ(req.version(), HttpRequest::Version::kUnknown);
    EXPECT_TRUE(req.path().empty());
    EXPECT_TRUE(req.query().empty());
    EXPECT_TRUE(req.body().empty());
    EXPECT_TRUE(req.headers().empty());
}

TEST(HttpRequestTest, SetAndGetMethod) {
    HttpRequest req;
    req.setMethod(HttpRequest::Method::kGet);
    EXPECT_EQ(req.method(), HttpRequest::Method::kGet);

    req.setMethod(HttpRequest::Method::kPost);
    EXPECT_EQ(req.method(), HttpRequest::Method::kPost);
}

TEST(HttpRequestTest, SetAndGetVersion) {
    HttpRequest req;
    req.setVersion(HttpRequest::Version::kHttp11);
    EXPECT_EQ(req.version(), HttpRequest::Version::kHttp11);
}

TEST(HttpRequestTest, SetAndGetPathAndQuery) {
    HttpRequest req;
    req.setPath("/index.html");
    req.setQuery("name=test&value=123");
    EXPECT_EQ(req.path(), "/index.html");
    EXPECT_EQ(req.query(), "name=test&value=123");
}

TEST(HttpRequestTest, AddAndGetHeader) {
    HttpRequest req;
    req.addHeader("Host", "localhost");
    req.addHeader("Content-Type", "text/html");

    EXPECT_EQ(req.getHeader("Host"), "localhost");
    EXPECT_EQ(req.getHeader("Content-Type"), "text/html");
    EXPECT_EQ(req.getHeader("NonExist"), "");
    EXPECT_EQ(req.headers().size(), 2u);
}

TEST(HttpRequestTest, SetAndAppendBody) {
    HttpRequest req;
    req.setBody("hello");
    EXPECT_EQ(req.body(), "hello");

    req.appendBody(" world");
    EXPECT_EQ(req.body(), "hello world");
}

// ==================== HttpResponse 测试 ====================

TEST(HttpResponseTest, BasicProperties) {
    HttpResponse resp(false);
    EXPECT_EQ(resp.statusCode(), HttpResponse::HttpStatusCode::kUnknown);
    EXPECT_FALSE(resp.closeConnection());

    resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
    resp.setStatusMessage("OK");
    EXPECT_EQ(resp.statusCode(), HttpResponse::HttpStatusCode::k200Ok);
    EXPECT_EQ(resp.statusMessage(), "OK");
}

TEST(HttpResponseTest, CloseConnection) {
    HttpResponse resp(true);
    EXPECT_TRUE(resp.closeConnection());

    resp.setCloseConnection(false);
    EXPECT_FALSE(resp.closeConnection());
}

TEST(HttpResponseTest, SetContentType) {
    HttpResponse resp(false);
    resp.setContentType("text/html");
    EXPECT_EQ(resp.getHeader("Content-Type"), "text/html");
}

TEST(HttpResponseTest, AddAndGetHeader) {
    HttpResponse resp(false);
    resp.addHeader("Server", "MyServer/1.0");
    EXPECT_EQ(resp.getHeader("Server"), "MyServer/1.0");
    EXPECT_EQ(resp.getHeader("NonExist"), "");
}

TEST(HttpResponseTest, AppendToBufferWithBody) {
    HttpResponse resp(false);
    resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
    resp.setStatusMessage("OK");
    resp.setContentType("text/plain");
    resp.setBody("Hello, World!");

    Buffer buf;
    resp.appendToBuffer(&buf);

    std::string output = buf.retrieveAllAsString();

    // 检查状态行
    EXPECT_NE(output.find("HTTP/1.1 200 OK\r\n"), std::string::npos);

    // 检查 Connection: keep-alive
    EXPECT_NE(output.find("Connection: keep-alive\r\n"), std::string::npos);

    // 检查 Content-Length
    EXPECT_NE(output.find("Content-Length: 13\r\n"), std::string::npos);

    // 检查 Content-Type
    EXPECT_NE(output.find("Content-Type: text/plain\r\n"), std::string::npos);

    // 检查空行分隔
    EXPECT_NE(output.find("\r\n\r\n"), std::string::npos);

    // 检查 Body
    EXPECT_NE(output.find("Hello, World!"), std::string::npos);

    // 验证完整的报文结构
    std::string expected_start = "HTTP/1.1 200 OK\r\n";
    EXPECT_EQ(output.substr(0, expected_start.size()), expected_start);

    // Body 应该在最后
    std::string expected_body = "Hello, World!";
    EXPECT_EQ(output.substr(output.size() - expected_body.size()), expected_body);
}

TEST(HttpResponseTest, AppendToBufferCloseConnection) {
    HttpResponse resp(true);
    resp.setStatusCode(HttpResponse::HttpStatusCode::k404NotFound);
    resp.setStatusMessage("Not Found");
    resp.setBody("404 Not Found");

    Buffer buf;
    resp.appendToBuffer(&buf);

    std::string output = buf.retrieveAllAsString();

    // 检查状态行
    EXPECT_NE(output.find("HTTP/1.1 404 Not Found\r\n"), std::string::npos);

    // 检查 Connection: close
    EXPECT_NE(output.find("Connection: close\r\n"), std::string::npos);

    // 检查 Content-Length
    EXPECT_NE(output.find("Content-Length: 13\r\n"), std::string::npos);

    // Body
    EXPECT_NE(output.find("404 Not Found"), std::string::npos);
}

TEST(HttpResponseTest, AppendToBufferNoBody) {
    HttpResponse resp(false);
    resp.setStatusCode(HttpResponse::HttpStatusCode::k200Ok);
    resp.setStatusMessage("OK");

    Buffer buf;
    resp.appendToBuffer(&buf);

    std::string output = buf.retrieveAllAsString();

    // 不应有 Content-Length
    EXPECT_EQ(output.find("Content-Length"), std::string::npos);

    // 应该以 \r\n\r\n 结尾
    EXPECT_EQ(output.substr(output.size() - 4), "\r\n\r\n");
}

TEST(HttpResponseTest, AppendToBufferBadRequest) {
    HttpResponse resp(true);
    resp.setStatusCode(HttpResponse::HttpStatusCode::k400BadRequest);
    resp.setStatusMessage("Bad Request");
    resp.setContentType("text/html");
    resp.setBody("<h1>Bad Request</h1>");

    Buffer buf;
    resp.appendToBuffer(&buf);

    std::string output = buf.retrieveAllAsString();

    EXPECT_NE(output.find("HTTP/1.1 400 Bad Request\r\n"), std::string::npos);
    EXPECT_NE(output.find("Connection: close\r\n"), std::string::npos);
    EXPECT_NE(output.find("Content-Length: 20\r\n"), std::string::npos);
    EXPECT_NE(output.find("Content-Type: text/html\r\n"), std::string::npos);
    EXPECT_NE(output.find("<h1>Bad Request</h1>"), std::string::npos);
}

}  // namespace httpserver
