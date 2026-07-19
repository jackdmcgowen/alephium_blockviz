#include "network/pch.h"
#include "network/alephium/http_transport.hpp"

#include <curl/curl.h>

size_t CurlHttpTransport::write_cb_(void* contents, size_t size, size_t nmemb, void* userp)
{
    const size_t total = size * nmemb;
    auto* out = static_cast<std::string*>(userp);
    out->append(static_cast<const char*>(contents), total);
    return total;
}

CurlHttpTransport::CurlHttpTransport()
{
    CURL* easy = curl_easy_init();
    easy_ = easy;
    if (!easy)
        return;
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &CurlHttpTransport::write_cb_);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
}

CurlHttpTransport::~CurlHttpTransport()
{
    if (easy_)
    {
        curl_easy_cleanup(static_cast<CURL*>(easy_));
        easy_ = nullptr;
    }
}

HttpResponse CurlHttpTransport::get(const std::string& url)
{
    HttpResponse r;
    if (!easy_ || url.empty())
        return r;
    CURL* easy = static_cast<CURL*>(easy_);
    std::string body;
    curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &body);
    const CURLcode code = curl_easy_perform(easy);
    if (code != CURLE_OK)
        return r;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &r.http_code);
    if (r.http_code == 200 && !body.empty())
    {
        r.ok = true;
        r.body = std::move(body);
    }
    return r;
}
