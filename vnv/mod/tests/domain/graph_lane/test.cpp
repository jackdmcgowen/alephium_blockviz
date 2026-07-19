#include "domain/block_scene.hpp"
#include "framework/expect.hpp"
#include "mod/tests/domain/_shared/make_block.hpp"

void test_graph_lane(VnvStats& s)
{
    std::printf("\n== graph lane mapping ==\n");
    BlockScene scene;
    // lane = from*4+to → 3*4+2 = 14
    scene.add_block(make_block("fff", 3, 2, 1));
    auto n = scene.graph().get("fff");
    VNV_EXPECT_MSG(s, n.has_value() && n->lane == 14, "lane 14 for 3->2");
    VNV_EXPECT_MSG(s, n->height == 1, "height 1");
}
