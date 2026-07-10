#pragma once

// Full AlphBlock storage for inspector continuity (K14).
// Populated at parse/BlockScene::add_block time alongside GraphDelta (sole detail path).
#include "alph_block.hpp"
#include "domain/block_graph.hpp"

#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

class AlphDetailStore
{
public:
    void upsert(const AlphBlock& block);
    void remove(const NodeId& id);
    void remove_many(const std::vector<NodeId>& ids);

    std::optional<AlphBlock> get(const NodeId& id) const;
    AlphBlock get_or_empty(const NodeId& id) const;

    size_t size() const;

private:
    mutable std::mutex mu_;
    std::unordered_map<NodeId, AlphBlock> by_id_;
};
