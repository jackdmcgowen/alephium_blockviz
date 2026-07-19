// Console unit tests: BlockScene confirm dual-write paths + AlphDetailStore slim.
// No GPU / Vulkan. Build: sln/blockviz_tests.vcxproj

#include "domain/block_scene.hpp"
#include "network/alephium/alph_detail_store.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
int g_fails = 0;

void expect(bool cond, const char* msg)
{
    if (!cond)
    {
        std::printf("FAIL: %s\n", msg);
        ++g_fails;
    }
    else
    {
        std::printf("ok: %s\n", msg);
    }
}

AlphBlock make_block(const char* hash, int from, int to, int height, int txn_n = 1)
{
    AlphBlock b;
    b.hash = hash;
    b.chainFrom = static_cast<uint8_t>(from);
    b.chainTo = static_cast<uint8_t>(to);
    b.height = height;
    b.timestamp = 1'000'000 + height * 8000;
    b.txn_count = txn_n;
    for (int i = 0; i < txn_n; ++i)
    {
        AlphTxn t;
        t.txid = std::string(hash) + "_tx" + std::to_string(i);
        t.gasAmount = 1;
        t.gasPrice = "1";
        b.txns.push_back(std::move(t));
    }
    return b;
}

void test_add_idempotent_and_confirm()
{
    std::printf("\n== add_block + mark_confirmed ==\n");
    BlockScene scene;
    AlphBlock a = make_block("aaa", 0, 0, 10, 2);
    expect(scene.add_block(a), "first add returns true");
    expect(!scene.add_block(a), "duplicate add returns false");
    expect(scene.graph().contains("aaa"), "graph contains hash");
    expect(scene.total_blocks() == 1, "total_blocks == 1");

    scene.mark_confirmed("aaa", /*lane=*/0, /*height=*/10, false);
    expect(scene.confirmed_height(0) == 10, "confirmed_height lane0 == 10");
    expect(scene.confirmed_tip_hash(0) == "aaa", "confirmed tip hash");
    {
        std::lock_guard<std::mutex> lock(scene.mutex());
        expect(scene.is_confirmed_locked("aaa"), "is_confirmed_locked");
        auto tips = scene.confirmed_frontier_ids_locked();
        expect(tips.size() == 1 && tips[0] == "aaa", "frontier ids size 1");
    }
}

void test_remove_erases_confirmed()
{
    std::printf("\n== remove_block erases confirmed ==\n");
    BlockScene scene;
    scene.add_block(make_block("bbb", 1, 1, 5));
    scene.mark_confirmed("bbb", 5, 5, false); // lane = 1*4+1 = 5
    expect(scene.confirmed_height(5) == 5, "height before remove");
    expect(scene.remove_block("bbb"), "remove returns true");
    expect(!scene.graph().contains("bbb"), "gone from graph");
    {
        std::lock_guard<std::mutex> lock(scene.mutex());
        expect(!scene.is_confirmed_locked("bbb"), "confirmed erased on remove");
    }
}

void test_confirm_readmit_after_remove()
{
    std::printf("\n== re-admit re-marks confirmed (poll path) ==\n");
    BlockScene scene;
    AlphBlock c = make_block("ccc", 0, 1, 3);
    scene.add_block(c);
    scene.mark_confirmed("ccc", /*lane=*/1, 3, false);
    scene.remove_block("ccc");
    expect(scene.add_block(c), "re-admit add true");
    // Poll path: if cache already main, mark again regardless of add result.
    scene.mark_confirmed("ccc", 1, 3, false);
    {
        std::lock_guard<std::mutex> lock(scene.mutex());
        expect(scene.is_confirmed_locked("ccc"), "re-admit mark_confirmed");
    }
    expect(scene.confirmed_tip_hash(1) == "ccc", "tip restored");
}

void test_detail_slim_pin()
{
    std::printf("\n== AlphDetailStore slim + pin ==\n");
    AlphDetailStore store;
    AlphBlock full = make_block("ddd", 0, 0, 1, 3);
    store.upsert(full);
    expect(store.size() == 1, "store size 1");
    expect(!store.is_slim("ddd"), "not slim initially");

    store.set_full_detail_pin("ddd");
    size_t n = store.prune_unpinned_txns();
    expect(n == 0, "pinned not slimmed");
    expect(!store.is_slim("ddd"), "still full when pinned");

    store.set_full_detail_pin("");
    n = store.prune_unpinned_txns();
    expect(n == 1, "unpinned slimmed once");
    expect(store.is_slim("ddd"), "is_slim after prune");
    auto got = store.get("ddd");
    expect(got.has_value() && got->txn_count == 3, "txn_count survives slim");
    expect(got->txns.empty(), "txns cleared");

    // Re-upsert full payload
    store.upsert(full);
    expect(!store.is_slim("ddd"), "full after re-upsert");
}

void test_detail_via_scene()
{
    std::printf("\n== scene detail_store slim via scene ==\n");
    BlockScene scene;
    scene.add_block(make_block("eee", 2, 2, 7, 2));
    expect(!scene.detail_store().is_slim("eee"), "detail full on add");
    scene.detail_store().set_full_detail_pin("eee");
    scene.detail_store().prune_unpinned_txns();
    expect(!scene.detail_store().is_slim("eee"), "pin protects");
    scene.detail_store().set_full_detail_pin("other");
    scene.detail_store().prune_unpinned_txns();
    expect(scene.detail_store().is_slim("eee"), "slim when unpinned");
}

void test_graph_lane()
{
    std::printf("\n== graph lane mapping ==\n");
    BlockScene scene;
    // lane = from*4+to → 3*4+2 = 14
    scene.add_block(make_block("fff", 3, 2, 1));
    auto n = scene.graph().get("fff");
    expect(n.has_value() && n->lane == 14, "lane 14 for 3->2");
    expect(n->height == 1, "height 1");
}

void test_prune_protects_frontier()
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
    expect(n >= 1, "pruned at least old block");
    expect(!scene.graph().contains("old"), "old dropped");
    expect(scene.graph().contains("tip"), "frontier tip protected");
    expect(scene.confirmed_tip_hash(0) == "tip", "tip still confirmed");
}
} // namespace

int main()
{
    std::printf("blockviz domain_tests (app/engine versions are independent)\n");
    test_add_idempotent_and_confirm();
    test_remove_erases_confirmed();
    test_confirm_readmit_after_remove();
    test_detail_slim_pin();
    test_detail_via_scene();
    test_graph_lane();
    test_prune_protects_frontier();

    std::printf("\n%s: %d failure(s)\n", g_fails ? "FAILED" : "PASSED", g_fails);
    return g_fails ? 1 : 0;
}
