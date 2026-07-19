#include "domain/block_scene.hpp"
#include "framework/expect.hpp"
#include "mod/tests/domain/_shared/make_block.hpp"

void test_prune_protects_frontier(VnvStats& s)
{
    std::printf("\n== prune time + protect frontier ==\n");
    BlockScene scene;
    AlphBlock oldb = make_block("old", 0, 0, 1);
    oldb.timestamp = 1000;
    AlphBlock tipb = make_block("tip", 0, 0, 2);
    tipb.timestamp = 9'000'000;
    scene.add_block(oldb);
    scene.add_block(tipb);
    scene.mark_confirmed("tip", /*lane=*/0, 2, false);
    const size_t n = scene.prune(/*min_ts=*/5'000'000, /*max=*/0);
    VNV_EXPECT_MSG(s, n >= 1, "pruned at least old block");
    VNV_EXPECT_MSG(s, !scene.graph().contains("old"), "old dropped");
    VNV_EXPECT_MSG(s, scene.graph().contains("tip"), "frontier tip protected");
    VNV_EXPECT_MSG(s, scene.confirmed_tip_hash(0) == "tip", "tip still confirmed");
}
