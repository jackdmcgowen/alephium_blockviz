#include "domain/block_scene.hpp"
#include "framework/expect.hpp"
#include "mod/tests/domain/_shared/make_block.hpp"

void test_remove_erases_confirmed(VnvStats& s)
{
    std::printf("\n== remove_block erases confirmed ==\n");
    BlockScene scene;
    scene.add_block(make_block("bbb", 1, 1, 5));
    scene.mark_confirmed("bbb", 5, 5, false); // lane = 1*4+1 = 5
    VNV_EXPECT_MSG(s, scene.confirmed_height(5) == 5, "height before remove");
    VNV_EXPECT_MSG(s, scene.remove_block("bbb"), "remove returns true");
    VNV_EXPECT_MSG(s, !scene.graph().contains("bbb"), "gone from graph");
    {
        std::lock_guard<std::mutex> lock(scene.mutex());
        VNV_EXPECT_MSG(s, !scene.is_confirmed_locked("bbb"), "confirmed erased on remove");
    }
}
