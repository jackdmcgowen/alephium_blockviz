#include "domain/block_scene.hpp"

#include <algorithm>

BlockScene::BlockScene()
{
    for (int i = 0; i < kLaneCount; ++i)
    {
        confirmed_height_[i] = -1;
        confirmed_hash_[i].clear();
        cursor_inited_[i] = false;
    }
}

bool BlockScene::add_block(cJSON* block)
{
    AlphBlock alph_block(block);

    std::lock_guard<std::mutex> lock(mu_);

    if (alph_block.hash.empty())
        return false;

    // Idempotent: poll windows overlap; re-seen hashes are a no-op (not an error).
    if (graph_.contains(alph_block.hash))
        return false;

    // Uncle eviction: drop uncles still live in the graph
    std::vector<NodeId> removed_uncles;
    for (const auto& unc : alph_block.uncles)
    {
        if (!unc.empty() && graph_.contains(unc))
            removed_uncles.push_back(unc);
    }

    GraphDelta delta;
    GraphNode node;
    node.id = alph_block.hash;
    node.timestamp_ms = alph_block.timestamp;
    node.height = alph_block.height;
    node.group_from = alph_block.chainFrom;
    node.group_to = alph_block.chainTo;
    node.lane = alph_block.chain_idx();
    node.lane_count_hint = 16;
    node.chain_label =
        std::to_string(alph_block.chainFrom) + "->" + std::to_string(alph_block.chainTo);
    delta.upsert_nodes.push_back(std::move(node));
    delta.remove_nodes = removed_uncles;

    graph_.apply(delta);
    detail_store_.upsert(alph_block);
    if (!removed_uncles.empty())
        detail_store_.remove_many(removed_uncles);

    // Uncle path does not call remove_block — erase confirmed under same lock.
    for (const NodeId& unc : removed_uncles)
        erase_confirmed_unlocked_(unc);

    RecentFeedItem item;
    item.hash = alph_block.hash;
    item.chainFrom = alph_block.chainFrom;
    item.chainTo = alph_block.chainTo;
    item.height = alph_block.height;
    feed_.push_back(std::move(item));

    // Drop feed rows for removed uncles
    if (!removed_uncles.empty())
    {
        feed_.erase(std::remove_if(feed_.begin(), feed_.end(),
                                   [&](const RecentFeedItem& f) {
                                       return std::find(removed_uncles.begin(), removed_uncles.end(),
                                                        f.hash) != removed_uncles.end();
                                   }),
                    feed_.end());
    }

    ++total_blocks_;
    if (feed_.size() > 120)
        feed_.pop_front();
    return true;
}

bool BlockScene::remove_block(const std::string& hash)
{
    if (hash.empty())
        return false;

    std::lock_guard<std::mutex> lock(mu_);

    if (!graph_.contains(hash))
        return false;

    GraphDelta delta;
    delta.remove_nodes.push_back(hash);
    graph_.apply(delta);
    detail_store_.remove(hash);
    erase_confirmed_unlocked_(hash);

    feed_.erase(std::remove_if(feed_.begin(), feed_.end(),
                               [&](const RecentFeedItem& b) { return b.hash == hash; }),
                feed_.end());
    return true;
}

void BlockScene::mark_confirmed(const NodeId& hash)
{
    if (hash.empty())
        return;
    std::lock_guard<std::mutex> lock(mu_);
    mark_confirmed_unlocked_(hash);
}

void BlockScene::mark_confirmed(const NodeId& hash, uint32_t lane, int height)
{
    if (hash.empty())
        return;
    std::lock_guard<std::mutex> lock(mu_);
    mark_confirmed_unlocked_(hash, lane, height);
}

void BlockScene::ensure_cursor_initialized(uint32_t lane, int start_height_minus_one)
{
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return;
    std::lock_guard<std::mutex> lock(mu_);
    if (cursor_inited_[lane])
        return;
    confirmed_height_[lane] = start_height_minus_one;
    confirmed_hash_[lane].clear();
    cursor_inited_[lane] = true;
}

int BlockScene::confirmed_height(uint32_t lane) const
{
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return -1;
    std::lock_guard<std::mutex> lock(mu_);
    return confirmed_height_[lane];
}

NodeId BlockScene::confirmed_tip_hash(uint32_t lane) const
{
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return {};
    std::lock_guard<std::mutex> lock(mu_);
    return confirmed_hash_[lane];
}

bool BlockScene::cursor_initialized(uint32_t lane) const
{
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return false;
    std::lock_guard<std::mutex> lock(mu_);
    return cursor_inited_[lane];
}

int BlockScene::try_advance_confirmed(uint32_t lane)
{
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return 0;
    std::lock_guard<std::mutex> lock(mu_);
    return try_advance_confirmed_unlocked_(lane);
}

bool BlockScene::is_confirmed_locked(const NodeId& hash) const
{
    return is_confirmed_unlocked_(hash);
}

std::vector<NodeId> BlockScene::confirmed_frontier_ids_locked() const
{
    std::vector<NodeId> out;
    out.reserve(kLaneCount);
    for (int i = 0; i < kLaneCount; ++i)
    {
        if (!confirmed_hash_[i].empty())
            out.push_back(confirmed_hash_[i]);
    }
    return out;
}

std::vector<NodeId> BlockScene::confirmed_tip_ids_locked() const
{
    return confirmed_frontier_ids_locked();
}

int BlockScene::confirmed_height_locked(uint32_t lane) const
{
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return -1;
    return confirmed_height_[lane];
}

bool BlockScene::is_frontier_hash_locked(const NodeId& hash) const
{
    if (hash.empty())
        return false;
    for (int i = 0; i < kLaneCount; ++i)
    {
        if (confirmed_hash_[i] == hash)
            return true;
    }
    return false;
}

void BlockScene::copy_confirmed_heights_locked(int out[kLaneCount]) const
{
    for (int i = 0; i < kLaneCount; ++i)
        out[i] = cursor_inited_[i] ? confirmed_height_[i] : -1;
}

void BlockScene::mark_confirmed_unlocked_(const NodeId& hash)
{
    if (hash.empty())
        return;
    confirmed_.insert(hash);

    // Resolve lane/height from graph for cursor advance.
    if (auto n = graph_.get(hash))
    {
        if (n->lane < static_cast<uint32_t>(kLaneCount) && n->height >= 0)
            try_advance_confirmed_unlocked_(static_cast<uint32_t>(n->lane));
    }
}

void BlockScene::mark_confirmed_unlocked_(const NodeId& hash, uint32_t lane, int height)
{
    if (hash.empty())
        return;
    confirmed_.insert(hash);
    if (lane >= static_cast<uint32_t>(kLaneCount) || !cursor_inited_[lane])
        return;

    // Fast path: exact next sequential height with known lane/height.
    if (height == confirmed_height_[lane] + 1)
    {
        confirmed_height_[lane] = height;
        confirmed_hash_[lane] = hash;
    }
    // Catch up any further contiguous confirmed heights already in the graph.
    try_advance_confirmed_unlocked_(lane);
}

void BlockScene::erase_confirmed_unlocked_(const NodeId& hash)
{
    if (hash.empty())
        return;
    confirmed_.erase(hash);
    // Monotonic: never decrease H_c. Clear hash_c if this was the frontier display hash,
    // then re-resolve another confirmed hash at the same height if present.
    for (int i = 0; i < kLaneCount; ++i)
    {
        if (confirmed_hash_[i] != hash)
            continue;
        confirmed_hash_[i].clear();
        if (confirmed_height_[i] >= 0)
        {
            const NodeId alt =
                find_confirmed_at_unlocked_(static_cast<uint32_t>(i), confirmed_height_[i]);
            if (!alt.empty())
                confirmed_hash_[i] = alt;
        }
    }
}

bool BlockScene::is_confirmed_unlocked_(const NodeId& hash) const
{
    if (hash.empty())
        return false;
    return confirmed_.count(hash) != 0;
}

NodeId BlockScene::find_confirmed_at_unlocked_(uint32_t lane, int height) const
{
    if (lane >= static_cast<uint32_t>(kLaneCount) || height < 0)
        return {};

    const std::vector<GraphNode> nodes = graph_.nodes_snapshot();
    for (const GraphNode& n : nodes)
    {
        if (n.lane != lane || n.height != height)
            continue;
        if (confirmed_.count(n.id))
            return n.id;
    }
    return {};
}

int BlockScene::try_advance_confirmed_unlocked_(uint32_t lane)
{
    if (lane >= static_cast<uint32_t>(kLaneCount) || !cursor_inited_[lane])
        return 0;

    int steps = 0;
    // Catch up through contiguous confirmed heights (budget via caller loops too).
    constexpr int kMaxSteps = 64;
    while (steps < kMaxSteps)
    {
        const int need = confirmed_height_[lane] + 1;
        const NodeId at = find_confirmed_at_unlocked_(lane, need);
        if (at.empty())
            break;
        confirmed_height_[lane] = need;
        confirmed_hash_[lane] = at;
        ++steps;
    }
    return steps;
}

std::vector<NodeId> BlockScene::tip_ids() const
{
    // Caller may hold mu_; graph has its own lock.
    const std::vector<GraphNode> nodes = graph_.nodes_snapshot();

    int64_t tip_height[16];
    for (int i = 0; i < 16; ++i)
        tip_height[i] = -1;
    std::vector<NodeId> tips_by_lane[16];

    for (const GraphNode& n : nodes)
    {
        if (n.lane >= 16 || n.height < 0)
            continue;
        const uint32_t lane = n.lane;
        if (n.height > tip_height[lane])
        {
            tip_height[lane] = n.height;
            tips_by_lane[lane].clear();
            tips_by_lane[lane].push_back(n.id);
        }
        else if (n.height == tip_height[lane])
        {
            tips_by_lane[lane].push_back(n.id);
        }
    }

    std::vector<NodeId> out;
    out.reserve(16);
    for (int i = 0; i < 16; ++i)
        for (const NodeId& id : tips_by_lane[i])
            out.push_back(id);
    return out;
}

AlphBlock BlockScene::resolve_detail(const std::string& hash) const
{
    if (auto d = detail_store_.get(hash))
        return *d;
    AlphBlock empty;
    empty.hash = hash;
    return empty;
}
