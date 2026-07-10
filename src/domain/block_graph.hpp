#pragma once

// Chain-agnostic block graph. No cJSON. No Vulkan. No txn lists.
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using NodeId = std::string; // typically block hash; adapters normalize

enum class EdgeKind : uint8_t
{
    Parent,
    Dependency, // e.g. Alephium deps[]
    Uncle,
    Reference
};

struct GraphNode
{
    NodeId   id;
    int64_t  timestamp_ms   = 0;
    int64_t  height         = -1;
    uint32_t group_from     = 0;
    uint32_t group_to       = 0;
    uint32_t lane           = 0; // e.g. chainFrom*4+chainTo
    uint32_t lane_count_hint = 1;
    std::string chain_label;
};

struct GraphEdge
{
    NodeId   from;
    NodeId   to;
    EdgeKind kind = EdgeKind::Dependency;
};

struct GraphDelta
{
    std::vector<GraphNode> upsert_nodes;
    std::vector<GraphEdge> upsert_edges; // optional v1 (may be empty)
    std::vector<NodeId>    remove_nodes;
};

class BlockGraph
{
public:
    void apply(const GraphDelta& delta);

    bool contains(const NodeId& id) const;
    std::optional<GraphNode> get(const NodeId& id) const;

    // Sorted by id for stable dual-write validation / layout
    std::vector<GraphNode> nodes_snapshot() const;
    std::vector<GraphEdge> edges_from(const NodeId& id) const;

    void prune(int64_t min_timestamp_ms, size_t max_nodes);

    size_t node_count() const;
    std::vector<NodeId> live_ids_sorted() const;

private:
    mutable std::mutex mu_;
    std::unordered_map<NodeId, GraphNode> nodes_;
    std::unordered_map<NodeId, std::vector<GraphEdge>> out_edges_;
};
