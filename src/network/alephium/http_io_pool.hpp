#pragma once

// Multi-threaded REST GET pool (timeline intervals + block-by-hash + is_main later).
// Workers only perform HTTP; poller/adapter drains results and applies policy.

#include "network/alephium/http_transport.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

class HttpIoPool
{
public:
    enum class Kind : int
    {
        BlockHash = 0,
        Interval  = 1,
        IsMain    = 2,
    };

    struct Result
    {
        Kind        kind = Kind::BlockHash;
        std::string hash;      // BlockHash / IsMain
        int64_t     from_ms = 0;
        int64_t     to_ms = 0;
        std::string body;
        bool        ok = false;
        long        http_code = 0;
        // IsMain: parse body on policy thread (JSON true/false)
    };

    // Optional factory: each worker calls once. Null → CurlHttpTransport per worker.
    // For tests: return shared FakeHttpTransport (must be thread-safe).
    using TransportFactory = std::function<std::shared_ptr<IHttpTransport>()>;

    HttpIoPool() = default;
    ~HttpIoPool() { stop(); }

    HttpIoPool(const HttpIoPool&) = delete;
    HttpIoPool& operator=(const HttpIoPool&) = delete;

    void start(const std::string& base_url, int workers = 6,
               TransportFactory factory = nullptr);
    void stop();

    bool enqueue_block_hash(const std::string& hash);
    // force: re-fetch even if key was completed before (live newest refresh).
    bool enqueue_interval(int64_t from_ms, int64_t to_ms, bool force = false);
    bool enqueue_is_main(const std::string& hash);

    std::vector<Result> drain_results(size_t max_n = 32);

    size_t pending_jobs() const;
    size_t in_flight() const; // queued + working
    size_t inflight_intervals() const;
    int    stats_ok() const { return stats_ok_.load(std::memory_order_relaxed); }
    int    stats_fail() const { return stats_fail_.load(std::memory_order_relaxed); }
    int    stats_enqueued() const { return stats_enqueued_.load(std::memory_order_relaxed); }

    void mark_block_failed(const std::string& hash);
    bool is_block_failed(const std::string& hash) const;

    void reset_stats();
    void clear_completed_intervals(); // domain switch / full reset
    // Allow re-enqueue after HTTP ok but policy-side parse/admit failure.
    void forget_completed_interval(int64_t from_ms);

    static constexpr size_t kMaxJobQueue = 256;
    static constexpr size_t kMaxResults = 512;
    static constexpr int kDefaultMaxInflightIntervals = 4;

    void set_max_inflight_intervals(int n) { max_inflight_intervals_ = n > 0 ? n : 1; }

private:
    struct Job
    {
        Kind        kind = Kind::BlockHash;
        std::string hash;
        int64_t     from_ms = 0;
        int64_t     to_ms = 0;
        std::string key; // dedupe key
    };

    void worker_main_(int worker_id);
    std::string make_block_url_(const std::string& hash) const;
    std::string make_interval_url_(int64_t from_ms, int64_t to_ms) const;
    std::string make_is_main_url_(const std::string& hash) const;

    std::string base_url_;
    TransportFactory factory_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{ false };

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Job> job_q_;
    std::unordered_set<std::string> queued_or_inflight_;
    std::unordered_set<std::string> completed_intervals_; // from_ms keys
    std::unordered_set<std::string> failed_blocks_;
    std::deque<Result> results_;
    int inflight_interval_count_ = 0;
    int max_inflight_intervals_ = kDefaultMaxInflightIntervals;

    std::atomic<int> stats_ok_{ 0 };
    std::atomic<int> stats_fail_{ 0 };
    std::atomic<int> stats_enqueued_{ 0 };
};
