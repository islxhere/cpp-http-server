#pragma once

// HttpContext — 维护单条连接的 HTTP 请求解析状态。
// 支持请求行、头部和固定 Content-Length 请求体的增量解析。

#include <cstddef>
#include <string>

#include "http/http_request.h"

namespace httpserver {

class Buffer;

class HttpContext {
public:
    enum class ParseState {
        kExpectRequestLine,
        kExpectHeaders,
        kExpectBody,
        kGotAll,
    };

    HttpContext();

    bool parseRequest(Buffer* buf);
    bool gotAll() const;
    void reset();

    ParseState state() const;
    const HttpRequest& request() const;
    HttpRequest& request();

private:
    bool processRequestLine(const std::string& line);
    bool processHeaderLine(const std::string& line);

    ParseState state_;
    HttpRequest request_;
    size_t expected_body_length_;
};

}  // namespace httpserver
