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

// Visual / consensus role for draw + confirm policy.
enum class BlockRole : uint8_t
{
    Unknown = 0,
    Main    = 1, // proven or assumed main-path
    Uncle   = 2  // ghost uncle / not-main competitor (kept for viz)
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
    int       txn_count     = -1; // from API; survives detail slim
    // Sum of output ALPH (atto digits); survives detail slim; empty = unknown.
    std::string alph_out_atto;
    BlockRole role          = BlockRole::Unknown;
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

    // Sorted by id for layout / tooling
    std::vector<GraphNode> nodes_snapshot() const;
    // Unsorted snapshot (cheaper per-frame layout path).
    std::vector<GraphNode> nodes_snapshot_unsorted() const;
    std::vector<GraphEdge> edges_from(const NodeId& id) const;

    void prune(int64_t min_timestamp_ms, size_t max_nodes);
    void clear();

    size_t node_count() const;
    std::vector<NodeId> live_ids_sorted() const;

    // Bumped on any structural mutate (admit/remove/prune/role upsert). Layout cache key.
    uint64_t generation() const;

private:
    mutable std::mutex mu_;
    std::unordered_map<NodeId, GraphNode> nodes_;
    std::unordered_map<NodeId, std::vector<GraphEdge>> out_edges_;
    uint64_t generation_ = 1;
};
