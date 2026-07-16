#pragma once

// Multi-threaded GET /blockflow/blocks/{hash} workers.
// Each worker owns a private CURL handle (never touches the poller's global curl).
// Results are drained on the poller/adapter thread for admit + policy.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

class BlockFetchPool
{
public:
    struct Result
    {
        std::string hash;
        std::string body; // raw JSON on success
        bool        ok = false;
        long        http_code = 0;
    };

    BlockFetchPool() = default;
    ~BlockFetchPool() { stop(); }

    BlockFetchPool(const BlockFetchPool&) = delete;
    BlockFetchPool& operator=(const BlockFetchPool&) = delete;

    // workers: default 3. base_url e.g. http://127.0.0.1:12973
    void start(const std::string& base_url, int workers = 3);
    void stop();

    // Enqueue a blockhash fetch. Returns false if already in-flight, failed, full, or empty.
    bool enqueue(const std::string& hash);

    // Drain completed results (call from poller thread).
    std::vector<Result> drain_results(size_t max_n = 32);

    size_t pending_jobs() const;
    size_t in_flight() const;
    int    stats_ok() const { return stats_ok_.load(std::memory_order_relaxed); }
    int    stats_fail() const { return stats_fail_.load(std::memory_order_relaxed); }
    int    stats_enqueued() const { return stats_enqueued_.load(std::memory_order_relaxed); }

    void mark_failed(const std::string& hash);
    bool is_failed(const std::string& hash) const;

    void reset_stats();

private:
    void worker_main_(int worker_id);
    static size_t write_cb_(void* contents, size_t size, size_t nmemb, void* userp);

    std::string base_url_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{ false };

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::string> job_q_;
    std::unordered_set<std::string> queued_or_inflight_;
    std::unordered_set<std::string> failed_;
    std::deque<Result> results_;

    std::atomic<int> stats_ok_{ 0 };
    std::atomic<int> stats_fail_{ 0 };
    std::atomic<int> stats_enqueued_{ 0 };

    static constexpr size_t kMaxJobQueue = 128;
    static constexpr size_t kMaxResults = 256;
};
