#pragma once

// HttpResponse — 表示一个待发送的 HTTP 响应。
// 提供设置状态码、响应头、响应体的接口，
// 并通过 appendToBuffer 将完整的 HTTP 报文写入 Buffer。

#include <string>
#include <unordered_map>

namespace httpserver {

class Buffer;

class HttpResponse {
public:
    enum class HttpStatusCode {
        kUnknown,
        k200Ok = 200,
        k400BadRequest = 400,
        k404NotFound = 404,
    };

    explicit HttpResponse(bool close_connection);

    void setStatusCode(HttpStatusCode code);
    HttpStatusCode statusCode() const;

    void setStatusMessage(const std::string& message);
    const std::string& statusMessage() const;

    void setCloseConnection(bool on);
    bool closeConnection() const;

    void setContentType(const std::string& content_type);
    void addHeader(const std::string& key, const std::string& value);
    std::string getHeader(const std::string& key) const;

    void setBody(const std::string& body);
    const std::string& body() const;

    // 将完整的 HTTP 响应报文追加到 output Buffer 中
    void appendToBuffer(Buffer* output) const;

private:
    HttpStatusCode status_code_;
    std::string status_message_;
    bool close_connection_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};

}  // namespace httpserver
