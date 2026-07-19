#include "domain/block_scene.hpp"
#include "framework/expect.hpp"
#include "mod/tests/domain/_shared/make_block.hpp"

void test_confirm_readmit_after_remove(VnvStats& s)
{
    std::printf("\n== re-admit re-marks confirmed (poll path) ==\n");
    BlockScene scene;
    AlphBlock c = make_block("ccc", 0, 1, 3);
    scene.add_block(c);
    scene.mark_confirmed("ccc", /*lane=*/1, 3, false);
    scene.remove_block("ccc");
    VNV_EXPECT_MSG(s, scene.add_block(c), "re-admit add true");
    // Poll path: if cache already main, mark again regardless of add result.
    scene.mark_confirmed("ccc", 1, 3, false);
    {
        std::lock_guard<std::mutex> lock(scene.mutex());
        VNV_EXPECT_MSG(s, scene.is_confirmed_locked("ccc"), "re-admit mark_confirmed");
    }
    VNV_EXPECT_MSG(s, scene.confirmed_tip_hash(1) == "ccc", "tip restored");
}
