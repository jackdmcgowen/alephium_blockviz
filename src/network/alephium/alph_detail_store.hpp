#pragma once

// Full AlphBlock storage for inspector continuity (K14).
// Populated at parse/BlockScene::add_block time alongside GraphDelta (sole detail path).
// PR11: optional slim policy — drop txn/UTXO payloads for unpinned (unselected) nodes.
#include "domain/alph_block.hpp"
#include "domain/block_graph.hpp"

#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct DetailStoreStats
{
    size_t entries     = 0;
    size_t full_blocks = 0; // entries with non-empty txns
    size_t slim_blocks = 0; // entries with empty txns
    size_t pruned_ops  = 0; // cumulative prune operations (blocks slimmed)
};

class AlphDetailStore
{
public:
    void upsert(const AlphBlock& block);
    void upsert(AlphBlock&& block);
    void remove(const NodeId& id);
    void remove_many(const std::vector<NodeId>& ids);
    void clear();

    // Full copy (inspector / pin paths). Prefer visit() on hot loops.
    std::optional<AlphBlock> get(const NodeId& id) const;
    AlphBlock get_or_empty(const NodeId& id) const;

    // Call fn(const AlphBlock&) under the store lock — no AlphBlock deep copy.
    // Do not call back into AlphDetailStore from fn (re-entrancy / deadlock).
    bool visit(const NodeId& id, const std::function<void(const AlphBlock&)>& fn) const;

    size_t size() const;
    DetailStoreStats stats() const;

    // --- PR11 slim policy ---
    // When enabled (default true), prune_unpinned_txns() clears txn payloads on
    // all entries except the full-detail pin (selection). Metadata + deps remain.
    void set_slim_enabled(bool enabled);
    bool slim_enabled() const;

    // Keep full txn payloads for this id (current selection). Empty clears pin.
    void set_full_detail_pin(const NodeId& id);
    NodeId full_detail_pin() const;

    // Clear txns on every entry that is not the full-detail pin.
    // Returns number of blocks slimmed this call.
    size_t prune_unpinned_txns();

    // True if entry exists and has no txn payloads.
    bool is_slim(const NodeId& id) const;

private:
    static void slim_inplace(AlphBlock& block);

    mutable std::mutex mu_;
    std::unordered_map<NodeId, AlphBlock> by_id_;
    NodeId full_pin_;
    bool   slim_enabled_ = true;
    size_t pruned_ops_   = 0;
};
