// mod_network suite driver — one folder per test under network/<id>/test.cpp
#include "framework/expect.hpp"

#include <cstdio>

void test_enqueue_block_and_drain(VnvStats& s);
void test_parallel_intervals(VnvStats& s);
void test_interval_dedupe(VnvStats& s);
void test_stop_clean(VnvStats& s);
void test_inflight_interval_cap(VnvStats& s);

int main()
{
    VnvStats stats;
    std::printf("mod_network: HttpIoPool (fake transport)\n");
    test_enqueue_block_and_drain(stats);
    test_parallel_intervals(stats);
    test_interval_dedupe(stats);
    test_stop_clean(stats);
    test_inflight_interval_cap(stats);
    std::printf("\nmod_network: %d passed, %d failed\n", stats.passes, stats.fails);
    return stats.fails == 0 ? 0 : 1;
}
