#include "domain/block_scene.hpp"
#include "framework/expect.hpp"
#include "mod/tests/domain/_shared/make_block.hpp"
#include "network/alephium/alph_detail_store.hpp"

void test_detail_slim_pin(VnvStats& s)
{
    std::printf("\n== AlphDetailStore slim + pin ==\n");
    AlphDetailStore store;
    AlphBlock full = make_block("ddd", 0, 0, 1, 3);
    store.upsert(full);
    VNV_EXPECT_MSG(s, store.size() == 1, "store size 1");
    VNV_EXPECT_MSG(s, !store.is_slim("ddd"), "not slim initially");

    store.set_full_detail_pin("ddd");
    size_t n = store.prune_unpinned_txns();
    VNV_EXPECT_MSG(s, n == 0, "pinned not slimmed");
    VNV_EXPECT_MSG(s, !store.is_slim("ddd"), "still full when pinned");

    store.set_full_detail_pin("");
    n = store.prune_unpinned_txns();
    VNV_EXPECT_MSG(s, n == 1, "unpinned slimmed once");
    VNV_EXPECT_MSG(s, store.is_slim("ddd"), "is_slim after prune");
    auto got = store.get("ddd");
    VNV_EXPECT_MSG(s, got.has_value() && got->txn_count == 3, "txn_count survives slim");
    VNV_EXPECT_MSG(s, got->txns.empty(), "txns cleared");
    // alph_out may be empty if make_block has no atto amounts — still must not crash.

    store.upsert(full);
    VNV_EXPECT_MSG(s, !store.is_slim("ddd"), "full after re-upsert");
}
