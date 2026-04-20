#include "http/http_request.h"

namespace httpserver {

HttpRequest::HttpRequest()
    : method_(Method::kInvalid),
      version_(Version::kUnknown) {}

void HttpRequest::setMethod(Method method) { method_ = method; }
HttpRequest::Method HttpRequest::method() const { return method_; }

void HttpRequest::setVersion(Version version) { version_ = version; }
HttpRequest::Version HttpRequest::version() const { return version_; }

void HttpRequest::setPath(const std::string& path) { path_ = path; }
const std::string& HttpRequest::path() const { return path_; }

void HttpRequest::setQuery(const std::string& query) { query_ = query; }
const std::string& HttpRequest::query() const { return query_; }

void HttpRequest::addHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

std::string HttpRequest::getHeader(const std::string& key) const {
    auto it = headers_.find(key);
    if (it != headers_.end()) {
        return it->second;
    }
    return {};
}

const std::unordered_map<std::string, std::string>& HttpRequest::headers() const {
    return headers_;
}

void HttpRequest::setBody(const std::string& body) { body_ = body; }

void HttpRequest::appendBody(const std::string& data) { body_.append(data); }

const std::string& HttpRequest::body() const { return body_; }

}  // namespace httpserver
