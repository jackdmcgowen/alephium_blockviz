#include "domain/block_scene.hpp"

#include <algorithm>
#include <cstdio>

void BlockScene::add_block(cJSON* block)
{
    AlphBlock alph_block(block);

    std::lock_guard<std::mutex> lock(mu_);

    if (alph_block.hash.empty())
        return;

    if (graph_.contains(alph_block.hash))
    {
        std::printf("duplicate\n");
        return;
    }

    // Uncle eviction: drop uncles still live in the graph (same policy as old chains path)
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

    feed_.erase(std::remove_if(feed_.begin(), feed_.end(),
                               [&](const RecentFeedItem& b) { return b.hash == hash; }),
                feed_.end());
    return true;
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

AlphBlock BlockScene::resolve_detail_under_lock(const std::string& hash) const
{
    return resolve_detail(hash);
}
