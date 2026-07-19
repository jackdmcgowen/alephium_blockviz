#include "network/pch.h"
#include "network/alephium/http_io_pool.hpp"

#include <cstdio>

void HttpIoPool::start(const std::string& base_url, int workers, TransportFactory factory)
{
    stop();
    base_url_ = base_url;
    factory_ = std::move(factory);
    if (base_url_.empty() && !factory_)
        return;
    while (!base_url_.empty() && base_url_.back() == '/')
        base_url_.pop_back();

    running_ = true;
    const int n = (workers < 1) ? 1 : ((workers > 8) ? 8 : workers);
    workers_.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        workers_.emplace_back(&HttpIoPool::worker_main_, this, i);

    std::printf("[http_io] pool started workers=%d url=%s\n", n,
                base_url_.empty() ? "(factory)" : base_url_.c_str());
}

void HttpIoPool::stop()
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        running_ = false;
        job_q_.clear();
        queued_or_inflight_.clear();
        inflight_interval_count_ = 0;
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

void HttpIoPool::reset_stats()
{
    stats_ok_.store(0, std::memory_order_relaxed);
    stats_fail_.store(0, std::memory_order_relaxed);
    stats_enqueued_.store(0, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mu_);
    failed_blocks_.clear();
}

void HttpIoPool::clear_completed_intervals()
{
    std::lock_guard<std::mutex> lock(mu_);
    completed_intervals_.clear();
}

bool HttpIoPool::enqueue_block_hash(const std::string& hash)
{
    if (hash.empty() || !running_.load(std::memory_order_relaxed))
        return false;
    const std::string key = std::string("b:") + hash;
    std::lock_guard<std::mutex> lock(mu_);
    if (failed_blocks_.count(hash) || queued_or_inflight_.count(key))
        return false;
    if (job_q_.size() >= kMaxJobQueue)
        return false;
    Job j;
    j.kind = Kind::BlockHash;
    j.hash = hash;
    j.key = key;
    job_q_.push_back(std::move(j));
    queued_or_inflight_.insert(key);
    stats_enqueued_.fetch_add(1, std::memory_order_relaxed);
    cv_.notify_one();
    return true;
}

bool HttpIoPool::enqueue_interval(int64_t from_ms, int64_t to_ms, bool force)
{
    if (to_ms <= from_ms || !running_.load(std::memory_order_relaxed))
        return false;
    const std::string key = std::string("i:") + std::to_string(from_ms);
    std::lock_guard<std::mutex> lock(mu_);
    if (!force && completed_intervals_.count(key))
        return false;
    if (queued_or_inflight_.count(key))
        return false;
    if (inflight_interval_count_ >= max_inflight_intervals_)
        return false;
    if (job_q_.size() >= kMaxJobQueue)
        return false;
    Job j;
    j.kind = Kind::Interval;
    j.from_ms = from_ms;
    j.to_ms = to_ms;
    j.key = key;
    job_q_.push_back(std::move(j));
    queued_or_inflight_.insert(key);
    ++inflight_interval_count_;
    stats_enqueued_.fetch_add(1, std::memory_order_relaxed);
    cv_.notify_one();
    return true;
}

bool HttpIoPool::enqueue_is_main(const std::string& hash)
{
    if (hash.empty() || !running_.load(std::memory_order_relaxed))
        return false;
    const std::string key = std::string("m:") + hash;
    std::lock_guard<std::mutex> lock(mu_);
    if (queued_or_inflight_.count(key))
        return false;
    if (job_q_.size() >= kMaxJobQueue)
        return false;
    Job j;
    j.kind = Kind::IsMain;
    j.hash = hash;
    j.key = key;
    job_q_.push_back(std::move(j));
    queued_or_inflight_.insert(key);
    stats_enqueued_.fetch_add(1, std::memory_order_relaxed);
    cv_.notify_one();
    return true;
}

void HttpIoPool::mark_block_failed(const std::string& hash)
{
    if (hash.empty())
        return;
    std::lock_guard<std::mutex> lock(mu_);
    failed_blocks_.insert(hash);
    queued_or_inflight_.erase(std::string("b:") + hash);
}

bool HttpIoPool::is_block_failed(const std::string& hash) const
{
    std::lock_guard<std::mutex> lock(mu_);
    return failed_blocks_.count(hash) != 0;
}

size_t HttpIoPool::pending_jobs() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return job_q_.size();
}

size_t HttpIoPool::in_flight() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return queued_or_inflight_.size();
}

size_t HttpIoPool::inflight_intervals() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return static_cast<size_t>(inflight_interval_count_);
}

std::vector<HttpIoPool::Result> HttpIoPool::drain_results(size_t max_n)
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

std::string HttpIoPool::make_block_url_(const std::string& hash) const
{
    return base_url_ + "/blockflow/blocks/" + hash;
}

std::string HttpIoPool::make_interval_url_(int64_t from_ms, int64_t to_ms) const
{
    return base_url_ + "/blockflow/blocks-with-events/?fromTs=" + std::to_string(from_ms) +
           "&toTs=" + std::to_string(to_ms);
}

std::string HttpIoPool::make_is_main_url_(const std::string& hash) const
{
    return base_url_ + "/blockflow/is-block-in-main-chain?blockHash=" + hash;
}

void HttpIoPool::worker_main_(int worker_id)
{
    std::shared_ptr<IHttpTransport> transport;
    if (factory_)
        transport = factory_();
    if (!transport)
        transport = std::make_shared<CurlHttpTransport>();

    while (true)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [&] {
                return !running_.load(std::memory_order_relaxed) || !job_q_.empty();
            });
            if (!running_.load(std::memory_order_relaxed) && job_q_.empty())
                break;
            if (job_q_.empty())
                continue;
            job = std::move(job_q_.front());
            job_q_.pop_front();
        }

        std::string url;
        switch (job.kind)
        {
        case Kind::BlockHash:
            url = make_block_url_(job.hash);
            break;
        case Kind::Interval:
            url = make_interval_url_(job.from_ms, job.to_ms);
            break;
        case Kind::IsMain:
            url = make_is_main_url_(job.hash);
            break;
        }

        // Factory-only tests may leave base_url empty; still call transport with full URL
        // if factory provides absolute URLs via side channel — use constructed url.
        HttpResponse resp = transport->get(url);

        Result r;
        r.kind = job.kind;
        r.hash = job.hash;
        r.from_ms = job.from_ms;
        r.to_ms = job.to_ms;
        r.ok = resp.ok;
        r.http_code = resp.http_code;
        if (resp.ok)
            r.body = std::move(resp.body);

        if (r.ok)
            stats_ok_.fetch_add(1, std::memory_order_relaxed);
        else
            stats_fail_.fetch_add(1, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(mu_);
            queued_or_inflight_.erase(job.key);
            if (job.kind == Kind::Interval)
            {
                if (inflight_interval_count_ > 0)
                    --inflight_interval_count_;
                if (r.ok)
                    completed_intervals_.insert(job.key);
            }
            if (job.kind == Kind::BlockHash && !r.ok)
                failed_blocks_.insert(job.hash);
            if (results_.size() >= kMaxResults)
                results_.pop_front();
            results_.push_back(std::move(r));
        }
    }
    (void)worker_id;
}
