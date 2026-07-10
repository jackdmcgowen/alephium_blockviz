#include "domain/block_scene.hpp"

#include <algorithm>
#include <cstdio>

BlockScene::BlockScene()
    : chains_(16)
{
}

void BlockScene::add_block(cJSON* block)
{
    AlphBlock alph_block(block);

    std::lock_guard<std::mutex> lock(mu_);

    const uint8_t chainIndex = alph_block.chain_idx();
    auto& heightMap = chains_[chainIndex];
    auto& blocksAtHeight = heightMap[alph_block.height];

    const auto result = blocksAtHeight.emplace(alph_block.hash, alph_block);
    if (result.second)
        feed_.push_back(alph_block);
    else
        std::printf("duplicate\n");

    std::vector<NodeId> removed_uncles;
    for (auto& bh : heightMap)
    {
        for (const auto& unc : alph_block.uncles)
        {
            auto uncle_find = bh.second.find(unc);
            if (uncle_find != bh.second.end())
            {
                removed_uncles.push_back(unc);
                bh.second.erase(unc);
            }
        }
    }

    GraphDelta delta;
    if (result.second)
    {
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
        detail_store_.upsert(alph_block);
    }
    if (!removed_uncles.empty())
    {
        delta.remove_nodes = removed_uncles;
        detail_store_.remove_many(removed_uncles);
    }
    if (!delta.upsert_nodes.empty() || !delta.remove_nodes.empty())
        graph_.apply(delta);

    if (dual_write_validate_)
    {
        std::vector<NodeId> chains_ids;
        chains_ids.reserve(4096);
        for (const auto& hm : chains_)
        {
            for (const auto& height_entry : hm)
            {
                for (const auto& hash_entry : height_entry.second)
                    chains_ids.push_back(hash_entry.first);
            }
        }
        std::sort(chains_ids.begin(), chains_ids.end());
        chains_ids.erase(std::unique(chains_ids.begin(), chains_ids.end()), chains_ids.end());
        const std::vector<NodeId> graph_ids = graph_.live_ids_sorted();
        if (chains_ids != graph_ids)
        {
            std::printf("[dual-write] hash-set mismatch: chains=%zu graph=%zu\n",
                        chains_ids.size(), graph_ids.size());
        }
    }

    ++total_blocks_;
    // Keep the most recent ~120 blocks in the feed (drop oldest).
    if (feed_.size() > 120)
        feed_.pop_front();
}

bool BlockScene::remove_block(const std::string& hash)
{
    if (hash.empty())
        return false;

    std::lock_guard<std::mutex> lock(mu_);

    bool erased = false;
    for (auto& heightMap : chains_)
    {
        for (auto hit = heightMap.begin(); hit != heightMap.end(); )
        {
            auto& blocks = hit->second;
            auto fit = blocks.find(hash);
            if (fit != blocks.end())
            {
                blocks.erase(fit);
                erased = true;
            }
            if (blocks.empty())
                hit = heightMap.erase(hit);
            else
                ++hit;
        }
    }

    if (!erased)
        return false;

    GraphDelta delta;
    delta.remove_nodes.push_back(hash);
    graph_.apply(delta);
    detail_store_.remove(hash);

    feed_.erase(std::remove_if(feed_.begin(), feed_.end(),
                               [&](const AlphBlock& b) { return b.hash == hash; }),
                feed_.end());
    return true;
}

AlphBlock BlockScene::find_in_chains_unlocked(const std::string& hash) const
{
    for (const auto& heightMap : chains_)
    {
        for (const auto& height_entry : heightMap)
        {
            auto it = height_entry.second.find(hash);
            if (it != height_entry.second.end())
                return it->second;
        }
    }
    AlphBlock empty;
    empty.hash = hash;
    return empty;
}

AlphBlock BlockScene::resolve_detail(const std::string& hash) const
{
    if (auto d = detail_store_.get(hash))
        return *d;

    std::lock_guard<std::mutex> lock(mu_);
    return find_in_chains_unlocked(hash);
}

AlphBlock BlockScene::resolve_detail_under_lock(const std::string& hash) const
{
    // Caller holds mutex(). Prefer detail store (own lock); chains fallback without re-lock.
    if (auto d = detail_store_.get(hash))
        return *d;
    return find_in_chains_unlocked(hash);
}
