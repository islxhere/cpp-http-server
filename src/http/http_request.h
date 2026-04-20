#pragma once

// HttpRequest — 表示一个解析后的 HTTP 请求。
// 包含请求方法、路径、查询参数、协议版本、请求头和请求体。

#include <string>
#include <unordered_map>

namespace httpserver {

class HttpRequest {
public:
    enum class Method {
        kInvalid,
        kGet,
        kPost,
        kPut,
        kDelete,
    };

    enum class Version {
        kUnknown,
        kHttp10,
        kHttp11,
    };

    HttpRequest();

    void setMethod(Method method);
    Method method() const;

    void setVersion(Version version);
    Version version() const;

    void setPath(const std::string& path);
    const std::string& path() const;

    void setQuery(const std::string& query);
    const std::string& query() const;

    void addHeader(const std::string& key, const std::string& value);
    std::string getHeader(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& headers() const;

    void setBody(const std::string& body);
    void appendBody(const std::string& data);
    const std::string& body() const;

private:
    Method method_;
    Version version_;
    std::string path_;
    std::string query_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};

}  // namespace httpserver
