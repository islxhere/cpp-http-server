#include <gtest/gtest.h>

#include "core/buffer.h"
#include "http/http_context.h"

namespace httpserver {

TEST(HttpContextTest, ParsesRequestSplitAcrossBuffers) {
    HttpContext context;
    Buffer buf;

    buf.append("POST /submit?debug=true HTTP/1.1\r\nHost: localhost\r\n");
    EXPECT_TRUE(context.parseRequest(&buf));
    EXPECT_FALSE(context.gotAll());
    EXPECT_EQ(context.state(), HttpContext::ParseState::kExpectHeaders);

    buf.append("Content-Length: 11\r\n\r\nhello");
    EXPECT_TRUE(context.parseRequest(&buf));
    EXPECT_FALSE(context.gotAll());
    EXPECT_EQ(context.state(), HttpContext::ParseState::kExpectBody);

    buf.append(" worldGET /next HTTP/1.1\r\n\r\n");
    EXPECT_TRUE(context.parseRequest(&buf));
    EXPECT_TRUE(context.gotAll());

    const HttpRequest& request = context.request();
    EXPECT_EQ(request.method(), HttpRequest::Method::kPost);
    EXPECT_EQ(request.version(), HttpRequest::Version::kHttp11);
    EXPECT_EQ(request.path(), "/submit");
    EXPECT_EQ(request.query(), "debug=true");
    EXPECT_EQ(request.getHeader("Host"), "localhost");
    EXPECT_EQ(request.getHeader("Content-Length"), "11");
    EXPECT_EQ(request.body(), "hello world");

    EXPECT_EQ(buf.retrieveAllAsString(), "GET /next HTTP/1.1\r\n\r\n");
}

TEST(HttpContextTest, RejectsInvalidContentLength) {
    HttpContext context;
    Buffer buf;

    buf.append("POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n");
    EXPECT_FALSE(context.parseRequest(&buf));
}

}  // namespace httpserver
