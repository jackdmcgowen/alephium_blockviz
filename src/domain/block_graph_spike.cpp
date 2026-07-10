// Spike S1: offline BlockGraph stress (no Vulkan).
// Built only when BLOCKGRAPH_SPIKE is defined (optional console tool path).
// Default app build does not compile this file.

#ifdef BLOCKGRAPH_SPIKE

#include "domain/block_graph.hpp"

#include <chrono>
#include <cstdio>
#include <string>

int main()
{
    BlockGraph g;
    constexpr int N = 10000;

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i)
    {
        GraphDelta d;
        GraphNode n;
        n.id = "h" + std::to_string(i);
        n.height = i;
        n.timestamp_ms = 1000 + i;
        n.lane = static_cast<uint32_t>(i % 16);
        n.lane_count_hint = 16;
        d.upsert_nodes.push_back(n);
        if (i > 0 && (i % 17) == 0)
        {
            d.remove_nodes.push_back("h" + std::to_string(i - 1));
        }
        g.apply(d);
    }
    g.prune(0, 5000);
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    std::printf("BlockGraph spike: applied %d deltas, live=%zu, elapsed=%lld ms\n",
                N, g.node_count(), static_cast<long long>(ms));
    return (ms < 50 || g.node_count() <= 5000) ? 0 : 1;
}

#endif
