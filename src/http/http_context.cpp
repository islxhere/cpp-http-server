#include "http/http_context.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>

#include "core/buffer.h"

namespace httpserver {
namespace {

const char* findCrlf(const Buffer* buf) {
    const char* begin = buf->peek();
    const char* end = begin + buf->readableBytes();
    return std::search(begin, end, "\r\n", "\r\n" + 2);
}

std::string trimOws(const std::string& value) {
    size_t first = 0;
    while (first < value.size() &&
           (value[first] == ' ' || value[first] == '\t')) {
        ++first;
    }

    size_t last = value.size();
    while (last > first &&
           (value[last - 1] == ' ' || value[last - 1] == '\t')) {
        --last;
    }
    return value.substr(first, last - first);
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

}  // namespace

HttpContext::HttpContext()
    : state_(ParseState::kExpectRequestLine), expected_body_length_(0) {}

bool HttpContext::parseRequest(Buffer* buf) {
    bool ok = true;
    bool has_more = true;

    while (has_more) {
        if (state_ == ParseState::kExpectRequestLine ||
            state_ == ParseState::kExpectHeaders) {
            const char* crlf = findCrlf(buf);
            if (crlf == buf->peek() + buf->readableBytes()) {
                has_more = false;
                continue;
            }

            std::string line(buf->peek(), crlf);
            buf->retrieveUntil(crlf + 2);

            if (state_ == ParseState::kExpectRequestLine) {
                ok = processRequestLine(line);
                if (!ok) break;
                state_ = ParseState::kExpectHeaders;
            } else if (line.empty()) {
                if (expected_body_length_ > 0) {
                    state_ = ParseState::kExpectBody;
                } else {
                    state_ = ParseState::kGotAll;
                    has_more = false;
                }
            } else {
                ok = processHeaderLine(line);
                if (!ok) break;
            }
        } else if (state_ == ParseState::kExpectBody) {
            if (buf->readableBytes() < expected_body_length_) {
                has_more = false;
            } else {
                request_.setBody(buf->retrieveAsString(expected_body_length_));
                state_ = ParseState::kGotAll;
                has_more = false;
            }
        } else {
            has_more = false;
        }
    }

    return ok;
}

bool HttpContext::gotAll() const { return state_ == ParseState::kGotAll; }

void HttpContext::reset() {
    state_ = ParseState::kExpectRequestLine;
    request_ = HttpRequest();
    expected_body_length_ = 0;
}

HttpContext::ParseState HttpContext::state() const { return state_; }
const HttpRequest& HttpContext::request() const { return request_; }
HttpRequest& HttpContext::request() { return request_; }

bool HttpContext::processRequestLine(const std::string& line) {
    const size_t first_space = line.find(' ');
    if (first_space == std::string::npos) return false;
    const size_t second_space = line.find(' ', first_space + 1);
    if (second_space == std::string::npos ||
        line.find(' ', second_space + 1) != std::string::npos) {
        return false;
    }

    const std::string method = line.substr(0, first_space);
    if (method == "GET") {
        request_.setMethod(HttpRequest::Method::kGet);
    } else if (method == "POST") {
        request_.setMethod(HttpRequest::Method::kPost);
    } else if (method == "PUT") {
        request_.setMethod(HttpRequest::Method::kPut);
    } else if (method == "DELETE") {
        request_.setMethod(HttpRequest::Method::kDelete);
    } else {
        return false;
    }

    const std::string target = line.substr(first_space + 1,
                                           second_space - first_space - 1);
    if (target.empty()) return false;
    const size_t query_pos = target.find('?');
    request_.setPath(target.substr(0, query_pos));
    if (query_pos != std::string::npos) {
        request_.setQuery(target.substr(query_pos + 1));
    }

    const std::string version = line.substr(second_space + 1);
    if (version == "HTTP/1.0") {
        request_.setVersion(HttpRequest::Version::kHttp10);
    } else if (version == "HTTP/1.1") {
        request_.setVersion(HttpRequest::Version::kHttp11);
    } else {
        return false;
    }

    return true;
}

bool HttpContext::processHeaderLine(const std::string& line) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) return false;

    const std::string key = line.substr(0, colon);
    const std::string value = trimOws(line.substr(colon + 1));
    request_.addHeader(key, value);

    if (toLower(key) == "content-length") {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
        if (end == value.c_str() || *end != '\0' ||
            parsed > std::numeric_limits<size_t>::max()) {
            return false;
        }
        expected_body_length_ = static_cast<size_t>(parsed);
    }

    return true;
}

}  // namespace httpserver
