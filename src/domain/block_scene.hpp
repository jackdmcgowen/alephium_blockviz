#pragma once

// Domain scene model (PR6b): owns chains, graph, detail store, feed.
// Network adapter writes; renderer reads under mutex. No Vulkan.
#include "alph_block.hpp"
#include "domain/block_graph.hpp"
#include "adapters/alephium/alph_detail_store.hpp"

#include <cjson/cJSON.h>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class BlockScene
{
public:
    using HashToBlocks = std::map<std::string, AlphBlock>;
    using HeightToHash = std::map<uint64_t, HashToBlocks>;

    BlockScene();

    // Network-thread ingest (thread-safe)
    void add_block(cJSON* block);
    // Returns true if a block with this hash was present and removed
    bool remove_block(const std::string& hash);

    std::mutex& mutex() { return mu_; }
    const std::mutex& mutex() const { return mu_; }

    // Caller must hold mutex()
    const std::vector<HeightToHash>& chains() const { return chains_; }
    std::vector<HeightToHash>& chains() { return chains_; }
    AlphDetailStore& detail_store() { return detail_store_; }
    const AlphDetailStore& detail_store() const { return detail_store_; }
    BlockGraph& graph() { return graph_; }
    const BlockGraph& graph() const { return graph_; }
    std::deque<AlphBlock>& feed() { return feed_; }
    const std::deque<AlphBlock>& feed() const { return feed_; }
    int total_blocks() const { return total_blocks_; }

    // Thread-safe: detail store first, then chains under mutex.
    AlphBlock resolve_detail(const std::string& hash) const;
    // Caller must hold mutex(); no re-lock of scene mutex.
    AlphBlock resolve_detail_under_lock(const std::string& hash) const;

private:
    AlphBlock find_in_chains_unlocked(const std::string& hash) const;

    mutable std::mutex mu_;
    std::vector<HeightToHash> chains_;
    BlockGraph graph_;
    AlphDetailStore detail_store_;
    std::deque<AlphBlock> feed_;
    int total_blocks_ = 0;
    bool dual_write_validate_ = false;
};
