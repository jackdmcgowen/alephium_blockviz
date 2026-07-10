#include "adapters/alephium/alph_detail_store.hpp"

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
}

void AlphDetailStore::remove_many(const std::vector<NodeId>& ids)
{
    std::lock_guard<std::mutex> lock(mu_);
    for (const NodeId& id : ids)
        by_id_.erase(id);
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
