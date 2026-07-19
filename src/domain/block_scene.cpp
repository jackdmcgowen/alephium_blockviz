#include "app/pch.h"
#include "domain/block_scene.hpp"

#include <algorithm>

BlockScene::BlockScene()
{
    for (int i = 0; i < kLaneCount; ++i)
    {
        confirmed_height_[i] = -1;
        confirmed_hash_[i].clear();
        frontier_valid_[i] = false;
        pending_hash_[i].clear();
        network_hud_.tip_height_by_lane[i] = -1;
    }
}

void BlockScene::reset()
{
    std::lock_guard<std::mutex> lock(mu_);
    graph_.clear();
    detail_store_.clear();
    feed_.clear();
    total_blocks_ = 0;
    confirmed_.clear();
    uncle_set_.clear();
    for (int i = 0; i < kLaneCount; ++i)
    {
        confirmed_height_[i] = -1;
        confirmed_hash_[i].clear();
        frontier_valid_[i] = false;
        pending_hash_[i].clear();
        frontier_walk_[i].clear();
        network_hud_.tip_height_by_lane[i] = -1;
    }
    trace_phase_ = 0;
    trace_offset_ = 0;
    genesis_ms_.store(ALPH_GENESIS_TIMESTAMP_MS_FALLBACK, std::memory_order_relaxed);
    timeline_origin_ms_.store(0, std::memory_order_relaxed);
    network_hud_.lookback_windows_done = 0;
    network_hud_.lookback_windows_need = 1;
    network_hud_.lanes_with_frontier = 0;
    network_hud_.open_confirm_walks = 0;
    network_hud_.stats_api_is_main = 0;
    network_hud_.stats_fetch_admitted = 0;
    network_hud_.stats_removed = 0;
    network_hud_.stats_seed_q = 0;
    network_hud_.last_poll_ms = 0;
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

    // Uncles are NOT auto-evicted here — adapter verifies is-in-main-chain and
    // marks non-main ghost uncles with BlockRole::Uncle (kept for viz).

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
    node.txn_count = alph_block.txn_count;
    if (uncle_set_.count(alph_block.hash))
        node.role = BlockRole::Uncle;
    delta.upsert_nodes.push_back(std::move(node));

    graph_.apply(delta);
    detail_store_.upsert(alph_block);

    RecentFeedItem item;
    item.hash = alph_block.hash;
    item.chainFrom = alph_block.chainFrom;
    item.chainTo = alph_block.chainTo;
    item.height = alph_block.height;
    feed_.push_back(std::move(item));

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
    uncle_set_.erase(hash);

    feed_.erase(std::remove_if(feed_.begin(), feed_.end(),
                               [&](const RecentFeedItem& b) { return b.hash == hash; }),
                feed_.end());
    return true;
}

void BlockScene::mark_uncle(const NodeId& hash)
{
    if (hash.empty())
        return;
    std::lock_guard<std::mutex> lock(mu_);
    uncle_set_.insert(hash);
    erase_confirmed_unlocked_(hash);
    auto node = graph_.get(hash);
    if (!node)
        return;
    node->role = BlockRole::Uncle;
    GraphDelta delta;
    delta.upsert_nodes.push_back(*node);
    graph_.apply(delta);
}

bool BlockScene::is_uncle(const NodeId& hash) const
{
    if (hash.empty())
        return false;
    std::lock_guard<std::mutex> lock(mu_);
    return uncle_set_.count(hash) != 0;
}

bool BlockScene::is_uncle_locked(const NodeId& hash) const
{
    if (hash.empty())
        return false;
    return uncle_set_.count(hash) != 0;
}

void BlockScene::mark_confirmed(const NodeId& hash)
{
    if (hash.empty())
        return;
    std::lock_guard<std::mutex> lock(mu_);
    mark_confirmed_unlocked_(hash);
}

void BlockScene::mark_confirmed(const NodeId& hash, uint32_t lane, int height, bool chain_walk)
{
    if (hash.empty())
        return;
    std::lock_guard<std::mutex> lock(mu_);
    mark_confirmed_unlocked_(hash, lane, height, chain_walk);
}

void BlockScene::set_frontier_walk(uint32_t lane, std::vector<NodeId> path_old_to_new)
{
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return;
    std::lock_guard<std::mutex> lock(mu_);
    frontier_walk_[lane] = std::move(path_old_to_new);
}

std::vector<NodeId> BlockScene::frontier_walk_locked(uint32_t lane) const
{
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return {};
    return frontier_walk_[lane];
}

void BlockScene::clear_frontier_walk_locked(uint32_t lane)
{
    // Caller holds mu_.
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return;
    frontier_walk_[lane].clear();
}

void BlockScene::set_pending_tip(uint32_t lane, const NodeId& hash)
{
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return;
    std::lock_guard<std::mutex> lock(mu_);
    pending_hash_[lane] = hash;
}

void BlockScene::clear_pending_tip(uint32_t lane)
{
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return;
    std::lock_guard<std::mutex> lock(mu_);
    pending_hash_[lane].clear();
}

NodeId BlockScene::pending_tip_hash(uint32_t lane) const
{
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return {};
    std::lock_guard<std::mutex> lock(mu_);
    return pending_hash_[lane];
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

bool BlockScene::frontier_valid(uint32_t lane) const
{
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return false;
    std::lock_guard<std::mutex> lock(mu_);
    return frontier_valid_[lane];
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

std::vector<NodeId> BlockScene::pending_tip_ids_locked() const
{
    std::vector<NodeId> out;
    out.reserve(kLaneCount);
    for (int i = 0; i < kLaneCount; ++i)
    {
        if (!pending_hash_[i].empty())
            out.push_back(pending_hash_[i]);
    }
    return out;
}

int BlockScene::confirmed_height_locked(uint32_t lane) const
{
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return -1;
    return confirmed_height_[lane];
}

void BlockScene::copy_confirmed_heights_locked(int out[kLaneCount]) const
{
    for (int i = 0; i < kLaneCount; ++i)
        out[i] = frontier_valid_[i] ? confirmed_height_[i] : -1;
}

void BlockScene::mark_confirmed_unlocked_(const NodeId& hash)
{
    if (hash.empty())
        return;
    confirmed_.insert(hash);
    if (auto n = graph_.get(hash))
    {
        if (n->lane < static_cast<uint32_t>(kLaneCount) && n->height >= 0)
            mark_confirmed_unlocked_(hash, static_cast<uint32_t>(n->lane),
                                     static_cast<int>(n->height));
    }
}

void BlockScene::mark_confirmed_unlocked_(const NodeId& hash, uint32_t lane, int height,
                                          bool chain_walk)
{
    if (hash.empty())
        return;
    // Always record bag membership (solid cubes / free-main labels).
    confirmed_.insert(hash);
    uncle_set_.erase(hash);
    if (auto n = graph_.get(hash))
    {
        if (n->role != BlockRole::Main)
        {
            n->role = BlockRole::Main;
            GraphDelta delta;
            delta.upsert_nodes.push_back(*n);
            graph_.apply(delta);
        }
    }
    if (lane >= static_cast<uint32_t>(kLaneCount) || height < 0)
        return;

    // Sequential frontier: first confirm sets H_c; later H_c+1 (or chain_walk jump).
    if (!frontier_valid_[lane])
    {
        frontier_valid_[lane] = true;
        confirmed_height_[lane] = height;
        confirmed_hash_[lane] = hash;
        if (pending_hash_[lane] == hash)
            pending_hash_[lane].clear();
        return;
    }

    if (height == confirmed_height_[lane])
    {
        confirmed_hash_[lane] = hash;
        if (pending_hash_[lane] == hash)
            pending_hash_[lane].clear();
        return;
    }

    if (height == confirmed_height_[lane] + 1 ||
        (chain_walk && height > confirmed_height_[lane]))
    {
        confirmed_height_[lane] = height;
        confirmed_hash_[lane] = hash;
        if (pending_hash_[lane] == hash)
            pending_hash_[lane].clear();
        return;
    }

    // height < H_c or height > H_c+1 without chain_walk: bag only.
}

void BlockScene::erase_confirmed_unlocked_(const NodeId& hash)
{
    if (hash.empty())
        return;
    confirmed_.erase(hash);

    for (int i = 0; i < kLaneCount; ++i)
    {
        if (pending_hash_[i] == hash)
            pending_hash_[i].clear();
        if (confirmed_hash_[i] == hash)
            refresh_frontier_lane_unlocked_(static_cast<uint32_t>(i));
    }
}

bool BlockScene::is_confirmed_unlocked_(const NodeId& hash) const
{
    if (hash.empty())
        return false;
    return confirmed_.count(hash) != 0;
}

void BlockScene::refresh_frontier_lane_unlocked_(uint32_t lane)
{
    if (lane >= static_cast<uint32_t>(kLaneCount))
        return;

    int best_h = -1;
    NodeId best;
    const std::vector<GraphNode> nodes = graph_.nodes_snapshot();
    for (const GraphNode& n : nodes)
    {
        if (n.lane != lane || n.height < 0)
            continue;
        if (!confirmed_.count(n.id))
            continue;
        if (static_cast<int>(n.height) > best_h)
        {
            best_h = static_cast<int>(n.height);
            best = n.id;
        }
    }

    if (best_h < 0)
    {
        confirmed_height_[lane] = -1;
        confirmed_hash_[lane].clear();
        frontier_valid_[lane] = false;
    }
    else
    {
        confirmed_height_[lane] = best_h;
        confirmed_hash_[lane] = best;
        frontier_valid_[lane] = true;
    }
}

size_t BlockScene::unconfirmed_live_count() const
{
    std::lock_guard<std::mutex> lock(mu_);
    const std::vector<GraphNode> nodes = graph_.nodes_snapshot();
    size_t n = 0;
    for (const GraphNode& node : nodes)
    {
        if (!confirmed_.count(node.id))
            ++n;
    }
    return n;
}

std::vector<NodeId> BlockScene::tip_ids() const
{
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

void BlockScene::set_trace_status(int phase, int offset)
{
    std::lock_guard<std::mutex> lock(mu_);
    trace_phase_ = phase;
    trace_offset_ = offset;
}

void BlockScene::set_network_hud(const NetworkHud& hud)
{
    std::lock_guard<std::mutex> lock(mu_);
    network_hud_ = hud;
}

BlockScene::NetworkHud BlockScene::network_hud() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return network_hud_;
}

BlockScene::NetworkHud BlockScene::network_hud_locked() const
{
    return network_hud_;
}

void BlockScene::get_trace_status_locked(int* phase_out, int* offset_out) const
{
    // Caller holds mu_.
    if (phase_out)
        *phase_out = trace_phase_;
    if (offset_out)
        *offset_out = trace_offset_;
}
