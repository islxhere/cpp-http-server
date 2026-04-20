#include "http/http_response.h"

#include "core/buffer.h"

namespace httpserver {

HttpResponse::HttpResponse(bool close_connection)
    : status_code_(HttpStatusCode::kUnknown),
      close_connection_(close_connection) {}

void HttpResponse::setStatusCode(HttpStatusCode code) { status_code_ = code; }
HttpResponse::HttpStatusCode HttpResponse::statusCode() const { return status_code_; }

void HttpResponse::setStatusMessage(const std::string& message) { status_message_ = message; }
const std::string& HttpResponse::statusMessage() const { return status_message_; }

void HttpResponse::setCloseConnection(bool on) { close_connection_ = on; }
bool HttpResponse::closeConnection() const { return close_connection_; }

void HttpResponse::setContentType(const std::string& content_type) {
    addHeader("Content-Type", content_type);
}

void HttpResponse::addHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

std::string HttpResponse::getHeader(const std::string& key) const {
    auto it = headers_.find(key);
    if (it != headers_.end()) {
        return it->second;
    }
    return {};
}

void HttpResponse::setBody(const std::string& body) { body_ = body; }
const std::string& HttpResponse::body() const { return body_; }

void HttpResponse::appendToBuffer(Buffer* output) const {
    // 状态行
    output->append("HTTP/1.1 ");
    output->append(std::to_string(static_cast<int>(status_code_)));
    output->append(" ");
    output->append(status_message_);
    output->append("\r\n");

    // Connection 头
    if (close_connection_) {
        output->append("Connection: close\r\n");
    } else {
        output->append("Connection: keep-alive\r\n");
    }

    // Content-Length 头（自动添加）
    if (!body_.empty()) {
        output->append("Content-Length: ");
        output->append(std::to_string(body_.size()));
        output->append("\r\n");
    }

    // 其他响应头
    for (const auto& header : headers_) {
        output->append(header.first);
        output->append(": ");
        output->append(header.second);
        output->append("\r\n");
    }

    // 空行分隔 Header 和 Body
    output->append("\r\n");

    // Body
    if (!body_.empty()) {
        output->append(body_);
    }
}

}  // namespace httpserver
