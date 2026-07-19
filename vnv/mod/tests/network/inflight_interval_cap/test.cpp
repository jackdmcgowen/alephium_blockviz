#include "framework/expect.hpp"
#include "mod/tests/network/_shared/helpers.hpp"
#include "network/alephium/http_io_pool.hpp"

void test_inflight_interval_cap(VnvStats& s)
{
    std::printf("\n== inflight interval cap ==\n");
    network_test_fake() = std::make_shared<FakeHttpTransport>();
    network_test_fake()->set_delay_ms(100);
    network_test_fake()->set_default_ok_body("{}");

    HttpIoPool pool;
    pool.set_max_inflight_intervals(2);
    pool.start("http://test.local", 4, network_test_factory());
    VNV_EXPECT(s, pool.enqueue_interval(10, 20, false));
    VNV_EXPECT(s, pool.enqueue_interval(20, 30, false));
    VNV_EXPECT(s, !pool.enqueue_interval(30, 40, false)); // cap
    wait_inflight_zero(pool);
    VNV_EXPECT(s, pool.enqueue_interval(30, 40, false));
    wait_inflight_zero(pool);
    pool.stop();
}
