#include "domain/block_scene.hpp"
#include "framework/expect.hpp"
#include "mod/tests/domain/_shared/make_block.hpp"

void test_add_idempotent_and_confirm(VnvStats& s)
{
    std::printf("\n== add_block + mark_confirmed ==\n");
    BlockScene scene;
    AlphBlock a = make_block("aaa", 0, 0, 10, 2);
    VNV_EXPECT_MSG(s, scene.add_block(a), "first add returns true");
    VNV_EXPECT_MSG(s, !scene.add_block(a), "duplicate add returns false");
    VNV_EXPECT_MSG(s, scene.graph().contains("aaa"), "graph contains hash");
    VNV_EXPECT_MSG(s, scene.total_blocks() == 1, "total_blocks == 1");

    scene.mark_confirmed("aaa", /*lane=*/0, /*height=*/10, false);
    VNV_EXPECT_MSG(s, scene.confirmed_height(0) == 10, "confirmed_height lane0 == 10");
    VNV_EXPECT_MSG(s, scene.confirmed_tip_hash(0) == "aaa", "confirmed tip hash");
    {
        std::lock_guard<std::mutex> lock(scene.mutex());
        VNV_EXPECT_MSG(s, scene.is_confirmed_locked("aaa"), "is_confirmed_locked");
        auto tips = scene.confirmed_frontier_ids_locked();
        VNV_EXPECT_MSG(s, tips.size() == 1 && tips[0] == "aaa", "frontier ids size 1");
    }
}
