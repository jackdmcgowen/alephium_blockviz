#pragma once

// Domain scene model: BlockGraph + AlphDetailStore + recent feed.
// Adapter writes; renderer reads under mutex.
#include "alph_block.hpp"
#include "domain/block_graph.hpp"
#include "adapters/alephium/alph_detail_store.hpp"

#include <cjson/cJSON.h>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

// Compact feed row (metadata only; full txns live in AlphDetailStore).
struct RecentFeedItem
{
    NodeId hash;
    int    chainFrom = 0;
    int    chainTo   = 0;
    int    height    = 0;
};

class BlockScene
{
public:
    static constexpr int kLaneCount = 16;

    BlockScene();

    // Network-thread ingest (thread-safe). Returns true if the hash was newly admitted.
    bool add_block(cJSON* block);
    // Returns true if a block with this hash was present and removed
    bool remove_block(const std::string& hash);

    // Network thread / adapter: self-locking (like add_block / remove_block).
    // Do NOT call while already holding mutex().
    void mark_confirmed(const NodeId& hash);
    // Preferred: lane + height known (updates highest-confirmed frontier for lane).
    void mark_confirmed(const NodeId& hash, uint32_t lane, int height);

    // Self-locking reads for adapter / HUD.
    int    confirmed_height(uint32_t lane) const;
    NodeId confirmed_tip_hash(uint32_t lane) const;
    bool   cursor_initialized(uint32_t lane) const;

    std::mutex& mutex() { return mu_; }
    const std::mutex& mutex() const { return mu_; }

    // Caller must hold mutex() for feed consistency with graph writes
    AlphDetailStore& detail_store() { return detail_store_; }
    const AlphDetailStore& detail_store() const { return detail_store_; }
    BlockGraph& graph() { return graph_; }
    const BlockGraph& graph() const { return graph_; }
    const std::deque<RecentFeedItem>& feed() const { return feed_; }
    int total_blocks() const { return total_blocks_; }

    // Live node snapshot (locks graph internally). Prefer under scene mutex for frame coherence.
    std::vector<GraphNode> nodes_snapshot() const { return graph_.nodes_snapshot(); }

    // Tip node ids per lane (max height); empty lanes omitted from result vector.
    // Caller may hold mutex(); uses graph snapshot.
    std::vector<NodeId> tip_ids() const;

    // Self-locking: count of live graph nodes not yet in confirmed_ set.
    size_t unconfirmed_live_count() const;

    // Presenter only: call while holding scene.mutex().
    bool is_confirmed_locked(const NodeId& hash) const;
    // Highest confirmed live block per lane (green Sobel / green tip arrows).
    std::vector<NodeId> confirmed_frontier_ids_locked() const;
    std::vector<NodeId> confirmed_tip_ids_locked() const;
    int confirmed_height_locked(uint32_t lane) const;
    bool is_frontier_hash_locked(const NodeId& hash) const;
    // Fill out[16]; -1 = no confirmed block on that lane yet.
    void copy_confirmed_heights_locked(int out[kLaneCount]) const;

    // Thread-safe detail resolve (AlphDetailStore only).
    AlphBlock resolve_detail(const std::string& hash) const;

private:
    void mark_confirmed_unlocked_(const NodeId& hash);
    void mark_confirmed_unlocked_(const NodeId& hash, uint32_t lane, int height);
    void erase_confirmed_unlocked_(const NodeId& hash);
    bool is_confirmed_unlocked_(const NodeId& hash) const;
    void refresh_frontier_lane_unlocked_(uint32_t lane);

    mutable std::mutex mu_;
    BlockGraph graph_;
    AlphDetailStore detail_store_;
    std::deque<RecentFeedItem> feed_;
    int total_blocks_ = 0;
    std::unordered_set<NodeId> confirmed_; // guarded by mu_

    // Highest confirmed live height/hash per lane (for HUD + green tip viz).
    int    confirmed_height_[kLaneCount]{};
    NodeId confirmed_hash_[kLaneCount]{};
    bool   frontier_valid_[kLaneCount]{};
};
