#include "adapters/alephium/block_fetch_pool.hpp"

#include <algorithm>
#include <curl/curl.h>
#include <cstdio>

size_t BlockFetchPool::write_cb_(void* contents, size_t size, size_t nmemb, void* userp)
{
    const size_t total = size * nmemb;
    auto* out = static_cast<std::string*>(userp);
    out->append(static_cast<const char*>(contents), total);
    return total;
}

void BlockFetchPool::start(const std::string& base_url, int workers)
{
    stop();
    base_url_ = base_url;
    if (base_url_.empty())
        return;
    // Strip trailing slash
    while (!base_url_.empty() && base_url_.back() == '/')
        base_url_.pop_back();

    running_ = true;
    const int n = (workers < 1) ? 1 : ((workers > 8) ? 8 : workers);
    workers_.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        workers_.emplace_back(&BlockFetchPool::worker_main_, this, i);

    std::printf("[fetch] pool started workers=%d url=%s\n", n, base_url_.c_str());
}

void BlockFetchPool::stop()
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        running_ = false;
        job_q_.clear();
        queued_or_inflight_.clear();
    }
    cv_.notify_all();
    for (auto& t : workers_)
    {
        if (t.joinable())
            t.join();
    }
    workers_.clear();
    std::lock_guard<std::mutex> lock(mu_);
    results_.clear();
}

void BlockFetchPool::reset_stats()
{
    stats_ok_.store(0, std::memory_order_relaxed);
    stats_fail_.store(0, std::memory_order_relaxed);
    stats_enqueued_.store(0, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mu_);
    failed_.clear();
}

bool BlockFetchPool::enqueue(const std::string& hash)
{
    if (hash.empty() || !running_.load(std::memory_order_relaxed))
        return false;

    std::lock_guard<std::mutex> lock(mu_);
    if (failed_.count(hash) || queued_or_inflight_.count(hash))
        return false;
    if (job_q_.size() >= kMaxJobQueue)
        return false;

    job_q_.push_back(hash);
    queued_or_inflight_.insert(hash);
    ++stats_enqueued_;
    cv_.notify_one();
    return true;
}

void BlockFetchPool::mark_failed(const std::string& hash)
{
    if (hash.empty())
        return;
    std::lock_guard<std::mutex> lock(mu_);
    failed_.insert(hash);
    queued_or_inflight_.erase(hash);
}

bool BlockFetchPool::is_failed(const std::string& hash) const
{
    std::lock_guard<std::mutex> lock(mu_);
    return failed_.count(hash) != 0;
}

size_t BlockFetchPool::pending_jobs() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return job_q_.size();
}

size_t BlockFetchPool::in_flight() const
{
    std::lock_guard<std::mutex> lock(mu_);
    // queued_or_inflight includes waiting jobs + workers currently fetching
    return queued_or_inflight_.size();
}

std::vector<BlockFetchPool::Result> BlockFetchPool::drain_results(size_t max_n)
{
    std::vector<Result> out;
    std::lock_guard<std::mutex> lock(mu_);
    while (!results_.empty() && out.size() < max_n)
    {
        out.push_back(std::move(results_.front()));
        results_.pop_front();
    }
    return out;
}

void BlockFetchPool::worker_main_(int worker_id)
{
    CURL* easy = curl_easy_init();
    if (!easy)
    {
        std::printf("[fetch] worker %d curl_easy_init failed\n", worker_id);
        return;
    }
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &BlockFetchPool::write_cb_);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);

    while (true)
    {
        std::string hash;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [&] {
                return !running_.load(std::memory_order_relaxed) || !job_q_.empty();
            });
            if (!running_.load(std::memory_order_relaxed) && job_q_.empty())
                break;
            if (job_q_.empty())
                continue;
            hash = std::move(job_q_.front());
            job_q_.pop_front();
            // keep in queued_or_inflight_ until result posted
        }

        Result r;
        r.hash = hash;
        r.ok = false;
        r.http_code = 0;

        // GET {base}/blockflow/blocks/{hash}
        char url[512];
        std::snprintf(url, sizeof(url), "%s/blockflow/blocks/%.64s", base_url_.c_str(),
                      hash.c_str());

        std::string body;
        curl_easy_setopt(easy, CURLOPT_URL, url);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, &body);

        const CURLcode code = curl_easy_perform(easy);
        if (code == CURLE_OK)
        {
            curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &r.http_code);
            if (r.http_code == 200 && !body.empty())
            {
                r.ok = true;
                r.body = std::move(body);
                stats_ok_.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                stats_fail_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        else
        {
            stats_fail_.fetch_add(1, std::memory_order_relaxed);
        }

        {
            std::lock_guard<std::mutex> lock(mu_);
            queued_or_inflight_.erase(hash);
            if (!r.ok)
                failed_.insert(hash);
            if (results_.size() >= kMaxResults)
                results_.pop_front();
            results_.push_back(std::move(r));
        }
    }

    curl_easy_cleanup(easy);
}
