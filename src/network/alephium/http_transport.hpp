#pragma once

// HTTP GET transport seam: production uses curl; VnV mod tests inject fakes.
// Workers and tests must not touch BlockScene / MainChainCache.

#include <cstdint>
#include <string>

struct HttpResponse
{
    bool        ok = false;
    long        http_code = 0;
    std::string body;
};

class IHttpTransport
{
public:
    virtual ~IHttpTransport() = default;
    // Blocking GET. Thread-safe if the implementation documents it (fakes must be).
    virtual HttpResponse get(const std::string& url) = 0;
};

// One easy-handle per instance; not shared across threads.
class CurlHttpTransport : public IHttpTransport
{
public:
    CurlHttpTransport();
    ~CurlHttpTransport() override;
    CurlHttpTransport(const CurlHttpTransport&) = delete;
    CurlHttpTransport& operator=(const CurlHttpTransport&) = delete;

    HttpResponse get(const std::string& url) override;

private:
    void* easy_ = nullptr; // CURL* without including curl in all TUs
    static size_t write_cb_(void* contents, size_t size, size_t nmemb, void* userp);
};
