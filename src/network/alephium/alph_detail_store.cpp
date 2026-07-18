#include "network/alephium/alph_detail_store.hpp"

void AlphDetailStore::slim_inplace(AlphBlock& block)
{
    // Keep hash/height/deps/uncles for layout arrows and feed; drop bulk UTXO trees.
    block.txns.clear();
    block.txns.shrink_to_fit();
}

void AlphDetailStore::upsert(const AlphBlock& block)
{
    if (block.hash.empty())
        return;
    std::lock_guard<std::mutex> lock(mu_);
    by_id_[block.hash] = block;
}

void AlphDetailStore::remove(const NodeId& id)
{
    std::lock_guard<std::mutex> lock(mu_);
    by_id_.erase(id);
    if (full_pin_ == id)
        full_pin_.clear();
}

void AlphDetailStore::remove_many(const std::vector<NodeId>& ids)
{
    std::lock_guard<std::mutex> lock(mu_);
    for (const NodeId& id : ids)
    {
        by_id_.erase(id);
        if (full_pin_ == id)
            full_pin_.clear();
    }
}

void AlphDetailStore::clear()
{
    std::lock_guard<std::mutex> lock(mu_);
    by_id_.clear();
    full_pin_.clear();
}

std::optional<AlphBlock> AlphDetailStore::get(const NodeId& id) const
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = by_id_.find(id);
    if (it == by_id_.end())
        return std::nullopt;
    return it->second;
}

AlphBlock AlphDetailStore::get_or_empty(const NodeId& id) const
{
    auto opt = get(id);
    if (opt)
        return *opt;
    return AlphBlock{};
}

size_t AlphDetailStore::size() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return by_id_.size();
}

DetailStoreStats AlphDetailStore::stats() const
{
    std::lock_guard<std::mutex> lock(mu_);
    DetailStoreStats s;
    s.entries = by_id_.size();
    s.pruned_ops = pruned_ops_;
    for (const auto& kv : by_id_)
    {
        if (kv.second.txns.empty())
            ++s.slim_blocks;
        else
            ++s.full_blocks;
    }
    return s;
}

void AlphDetailStore::set_slim_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mu_);
    slim_enabled_ = enabled;
}

bool AlphDetailStore::slim_enabled() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return slim_enabled_;
}

void AlphDetailStore::set_full_detail_pin(const NodeId& id)
{
    std::lock_guard<std::mutex> lock(mu_);
    full_pin_ = id;
}

NodeId AlphDetailStore::full_detail_pin() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return full_pin_;
}

size_t AlphDetailStore::prune_unpinned_txns()
{
    std::lock_guard<std::mutex> lock(mu_);
    if (!slim_enabled_)
        return 0;

    size_t slimmed = 0;
    for (auto& kv : by_id_)
    {
        if (!full_pin_.empty() && kv.first == full_pin_)
            continue;
        if (kv.second.txns.empty())
            continue;
        slim_inplace(kv.second);
        ++slimmed;
        ++pruned_ops_;
    }
    return slimmed;
}

bool AlphDetailStore::is_slim(const NodeId& id) const
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = by_id_.find(id);
    if (it == by_id_.end())
        return false;
    return it->second.txns.empty();
}
