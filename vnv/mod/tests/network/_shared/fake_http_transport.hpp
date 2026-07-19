#pragma once

// Thread-safe fake HTTP transport for mod_network (no live node).

#include "network/alephium/http_transport.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class FakeHttpTransport : public IHttpTransport
{
public:
    void set_delay_ms(int ms) { delay_ms_ = ms; }

    void set_response(const std::string& url_substr, HttpResponse resp)
    {
        std::lock_guard<std::mutex> lock(mu_);
        routes_[url_substr] = std::move(resp);
    }

    void set_default_ok_body(std::string body)
    {
        std::lock_guard<std::mutex> lock(mu_);
        default_body_ = std::move(body);
        has_default_ = true;
    }

    int call_count() const { return calls_.load(std::memory_order_relaxed); }
    int max_concurrent() const { return max_concurrent_.load(std::memory_order_relaxed); }

    std::vector<std::string> urls() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return urls_;
    }

    HttpResponse get(const std::string& url) override
    {
        calls_.fetch_add(1, std::memory_order_relaxed);
        const int cur = concurrent_.fetch_add(1, std::memory_order_relaxed) + 1;
        int prev = max_concurrent_.load(std::memory_order_relaxed);
        while (cur > prev &&
               !max_concurrent_.compare_exchange_weak(prev, cur, std::memory_order_relaxed))
        {
        }

        {
            std::lock_guard<std::mutex> lock(mu_);
            urls_.push_back(url);
        }

        if (delay_ms_ > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));

        HttpResponse out;
        {
            std::lock_guard<std::mutex> lock(mu_);
            for (const auto& kv : routes_)
            {
                if (url.find(kv.first) != std::string::npos)
                {
                    out = kv.second;
                    concurrent_.fetch_sub(1, std::memory_order_relaxed);
                    return out;
                }
            }
            if (has_default_)
            {
                out.ok = true;
                out.http_code = 200;
                out.body = default_body_;
            }
        }
        concurrent_.fetch_sub(1, std::memory_order_relaxed);
        return out;
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, HttpResponse> routes_;
    std::string default_body_;
    bool has_default_ = false;
    int delay_ms_ = 0;
    std::atomic<int> calls_{ 0 };
    std::atomic<int> concurrent_{ 0 };
    std::atomic<int> max_concurrent_{ 0 };
    std::vector<std::string> urls_;
};
