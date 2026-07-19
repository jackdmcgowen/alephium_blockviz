#include "domain/alph_block.hpp"
#include "framework/expect.hpp"
#include "network/alephium/alph_detail_store.hpp"

void test_alph_out_sum_slim(VnvStats& s)
{
    std::printf("\n== ALPH out sum + slim preserve ==\n");

    AlphTxn tx;
    UTXO o1;
    o1.attoAlphAmount = "1000000000000000000"; // 1 ALPH
    UTXO o2;
    o2.attoAlphAmount = "500000000000000000"; // 0.5 ALPH
    tx.outputs.push_back(o1);
    tx.outputs.push_back(o2);
    tx.alph_out_atto = alph_sum_txn_outputs(tx);
    VNV_EXPECT_MSG(s, tx.alph_out_atto == "1500000000000000000", "txn sum 1.5 ALPH atto");
    VNV_EXPECT_MSG(s, alph_atto_to_display(tx.alph_out_atto) == "1.5", "display 1.5");

    AlphBlock b;
    b.hash = "eee";
    b.txns.push_back(tx);
    AlphTxn tx2;
    UTXO o3;
    o3.attoAlphAmount = "2000000000000000000";
    tx2.outputs.push_back(o3);
    tx2.alph_out_atto = alph_sum_txn_outputs(tx2);
    b.txns.push_back(tx2);
    b.alph_out_atto = alph_sum_block_outputs(b);
    VNV_EXPECT_MSG(s, b.alph_out_atto == "3500000000000000000", "block sum 3.5 ALPH");

    AlphDetailStore store;
    store.upsert(b);
    store.prune_unpinned_txns();
    auto got = store.get("eee");
    VNV_EXPECT_MSG(s, got.has_value() && got->txns.empty(), "slim clears txns");
    VNV_EXPECT_MSG(s, got->alph_out_atto == "3500000000000000000", "alph_out survives slim");

    VNV_EXPECT_MSG(s, alph_cmp_atto("2", "10") < 0, "cmp length");
    VNV_EXPECT_MSG(s, alph_cmp_atto(alph_from_double_to_atto(1.5), "1500000000000000000") == 0,
                   "double 1.5 → atto");
}
