#include "domain/block_graph.hpp"

#include <algorithm>

void BlockGraph::apply(const GraphDelta& delta)
{
    std::lock_guard<std::mutex> lock(mu_);

    for (const NodeId& id : delta.remove_nodes)
    {
        nodes_.erase(id);
        out_edges_.erase(id);
        for (auto& kv : out_edges_)
        {
            auto& edges = kv.second;
            edges.erase(std::remove_if(edges.begin(), edges.end(),
                                       [&](const GraphEdge& e) { return e.to == id || e.from == id; }),
                        edges.end());
        }
    }

    for (const GraphNode& n : delta.upsert_nodes)
    {
        if (n.id.empty())
            continue;
        nodes_[n.id] = n;
    }

    for (const GraphEdge& e : delta.upsert_edges)
    {
        if (e.from.empty() || e.to.empty())
            continue;
        auto& list = out_edges_[e.from];
        bool exists = false;
        for (const GraphEdge& existing : list)
        {
            if (existing.to == e.to && existing.kind == e.kind)
            {
                exists = true;
                break;
            }
        }
        if (!exists)
            list.push_back(e);
    }
}

bool BlockGraph::contains(const NodeId& id) const
{
    std::lock_guard<std::mutex> lock(mu_);
    return nodes_.find(id) != nodes_.end();
}

std::optional<GraphNode> BlockGraph::get(const NodeId& id) const
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = nodes_.find(id);
    if (it == nodes_.end())
        return std::nullopt;
    return it->second;
}

std::vector<GraphNode> BlockGraph::nodes_snapshot() const
{
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<GraphNode> out;
    out.reserve(nodes_.size());
    for (const auto& kv : nodes_)
        out.push_back(kv.second);
    std::sort(out.begin(), out.end(),
              [](const GraphNode& a, const GraphNode& b) { return a.id < b.id; });
    return out;
}

std::vector<GraphEdge> BlockGraph::edges_from(const NodeId& id) const
{
    std::lock_guard<std::mutex> lock(mu_);
    auto it = out_edges_.find(id);
    if (it == out_edges_.end())
        return {};
    return it->second;
}

void BlockGraph::prune(int64_t min_timestamp_ms, size_t max_nodes)
{
    std::lock_guard<std::mutex> lock(mu_);

    if (min_timestamp_ms > 0)
    {
        std::vector<NodeId> drop;
        for (const auto& kv : nodes_)
        {
            if (kv.second.timestamp_ms > 0 && kv.second.timestamp_ms < min_timestamp_ms)
                drop.push_back(kv.first);
        }
        for (const NodeId& id : drop)
        {
            nodes_.erase(id);
            out_edges_.erase(id);
        }
    }

    if (max_nodes > 0 && nodes_.size() > max_nodes)
    {
        std::vector<GraphNode> sorted;
        sorted.reserve(nodes_.size());
        for (const auto& kv : nodes_)
            sorted.push_back(kv.second);
        std::sort(sorted.begin(), sorted.end(),
                  [](const GraphNode& a, const GraphNode& b) {
                      if (a.timestamp_ms != b.timestamp_ms)
                          return a.timestamp_ms < b.timestamp_ms;
                      return a.id < b.id;
                  });
        const size_t remove_count = nodes_.size() - max_nodes;
        for (size_t i = 0; i < remove_count; ++i)
        {
            nodes_.erase(sorted[i].id);
            out_edges_.erase(sorted[i].id);
        }
    }
}

void BlockGraph::clear()
{
    std::lock_guard<std::mutex> lock(mu_);
    nodes_.clear();
    out_edges_.clear();
}

size_t BlockGraph::node_count() const
{
    std::lock_guard<std::mutex> lock(mu_);
    return nodes_.size();
}

std::vector<NodeId> BlockGraph::live_ids_sorted() const
{
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<NodeId> ids;
    ids.reserve(nodes_.size());
    for (const auto& kv : nodes_)
        ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}
