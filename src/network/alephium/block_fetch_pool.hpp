#pragma once

// Compatibility facade over HttpIoPool for block-hash GET workers.
// Prefer HttpIoPool for new code (intervals, is_main).

#include "network/alephium/http_io_pool.hpp"

#include <string>
#include <vector>

class BlockFetchPool
{
public:
    using Result = HttpIoPool::Result;

    void start(const std::string& base_url, int workers = 3)
    {
        pool_.start(base_url, workers, nullptr);
    }
    void stop() { pool_.stop(); }

    bool enqueue(const std::string& hash) { return pool_.enqueue_block_hash(hash); }
    std::vector<Result> drain_results(size_t max_n = 32) { return pool_.drain_results(max_n); }

    size_t pending_jobs() const { return pool_.pending_jobs(); }
    size_t in_flight() const { return pool_.in_flight(); }
    int    stats_ok() const { return pool_.stats_ok(); }
    int    stats_fail() const { return pool_.stats_fail(); }
    int    stats_enqueued() const { return pool_.stats_enqueued(); }

    void mark_failed(const std::string& hash) { pool_.mark_block_failed(hash); }
    bool is_failed(const std::string& hash) const { return pool_.is_block_failed(hash); }
    void reset_stats() { pool_.reset_stats(); }

    HttpIoPool& io() { return pool_; }
    const HttpIoPool& io() const { return pool_; }

private:
    HttpIoPool pool_;
};
