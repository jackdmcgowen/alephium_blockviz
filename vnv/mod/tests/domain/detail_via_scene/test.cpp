#include "domain/block_scene.hpp"
#include "framework/expect.hpp"
#include "mod/tests/domain/_shared/make_block.hpp"

void test_detail_via_scene(VnvStats& s)
{
    std::printf("\n== scene detail_store slim via scene ==\n");
    BlockScene scene;
    scene.add_block(make_block("eee", 2, 2, 7, 2));
    VNV_EXPECT_MSG(s, !scene.detail_store().is_slim("eee"), "detail full on add");
    scene.detail_store().set_full_detail_pin("eee");
    scene.detail_store().prune_unpinned_txns();
    VNV_EXPECT_MSG(s, !scene.detail_store().is_slim("eee"), "pin protects");
    scene.detail_store().set_full_detail_pin("other");
    scene.detail_store().prune_unpinned_txns();
    VNV_EXPECT_MSG(s, scene.detail_store().is_slim("eee"), "slim when unpinned");
}
