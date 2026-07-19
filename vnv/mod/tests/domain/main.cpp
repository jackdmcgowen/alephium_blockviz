// mod_domain suite driver — one folder per test under domain/<id>/test.cpp
#include "framework/expect.hpp"

#include <cstdio>

void test_add_idempotent_and_confirm(VnvStats& s);
void test_remove_erases_confirmed(VnvStats& s);
void test_confirm_readmit_after_remove(VnvStats& s);
void test_detail_slim_pin(VnvStats& s);
void test_detail_via_scene(VnvStats& s);
void test_graph_lane(VnvStats& s);
void test_prune_protects_frontier(VnvStats& s);
void test_alph_out_sum_slim(VnvStats& s);

int main()
{
    VnvStats s;
    std::printf("mod_domain: BlockScene / detail store\n");
    test_add_idempotent_and_confirm(s);
    test_remove_erases_confirmed(s);
    test_confirm_readmit_after_remove(s);
    test_detail_slim_pin(s);
    test_detail_via_scene(s);
    test_graph_lane(s);
    test_prune_protects_frontier(s);
    test_alph_out_sum_slim(s);
    std::printf("\nmod_domain: %d passed, %d failed\n", s.passes, s.fails);
    return s.fails == 0 ? 0 : 1;
}
