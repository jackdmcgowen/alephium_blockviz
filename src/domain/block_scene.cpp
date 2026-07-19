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
    return add_block(AlphBlock(block));
}

bool BlockScene::add_block(const AlphBlock& alph_block)
{
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
    node.alph_out_atto = alph_block.alph_out_atto;
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

size_t BlockScene::prune(int64_t min_timestamp_ms, size_t max_nodes)
{
    std::lock_guard<std::mutex> lock(mu_);

    std::unordered_set<NodeId> protect;
    for (int i = 0; i < kLaneCount; ++i)
    {
        if (!confirmed_hash_[i].empty())
            protect.insert(confirmed_hash_[i]);
        if (!pending_hash_[i].empty())
            protect.insert(pending_hash_[i]);
        for (const NodeId& h : frontier_walk_[i])
            if (!h.empty())
                protect.insert(h);
    }

    std::vector<NodeId> drop;
    const auto snap = graph_.nodes_snapshot(); // sorts; holds no scene mu_ internally but graph has own mu
    // nodes_snapshot locks graph mu_ — we already hold scene mu_ which orders: scene then graph in other paths?
    // BlockScene methods always take mu_ then call graph_ which takes its own mu_ — OK (different mutexes).

    for (const GraphNode& n : snap)
    {
        if (protect.count(n.id))
            continue;
        if (min_timestamp_ms > 0 && n.timestamp_ms > 0 && n.timestamp_ms < min_timestamp_ms)
            drop.push_back(n.id);
    }

    // Count cap: drop oldest non-protected after time prune.
    if (max_nodes > 0)
    {
        std::vector<GraphNode> remain;
        remain.reserve(snap.size());
        std::unordered_set<NodeId> dropping(drop.begin(), drop.end());
        for (const GraphNode& n : snap)
        {
            if (dropping.count(n.id))
                continue;
            remain.push_back(n);
        }
        if (remain.size() > max_nodes)
        {
            std::sort(remain.begin(), remain.end(),
                      [](const GraphNode& a, const GraphNode& b) {
                          if (a.timestamp_ms != b.timestamp_ms)
                              return a.timestamp_ms < b.timestamp_ms;
                          return a.id < b.id;
                      });
            size_t need = remain.size() - max_nodes;
            for (const GraphNode& n : remain)
            {
                if (need == 0)
                    break;
                if (protect.count(n.id))
                    continue;
                drop.push_back(n.id);
                --need;
            }
        }
    }

    if (drop.empty())
        return 0;

    GraphDelta delta;
    delta.remove_nodes = drop;
    graph_.apply(delta);
    detail_store_.remove_many(drop);
    for (const NodeId& id : drop)
    {
        erase_confirmed_unlocked_(id);
        uncle_set_.erase(id);
    }
    feed_.erase(std::remove_if(feed_.begin(), feed_.end(),
                               [&](const RecentFeedItem& b) {
                                   return std::find(drop.begin(), drop.end(), b.hash) != drop.end();
                               }),
                feed_.end());
    return drop.size();
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
