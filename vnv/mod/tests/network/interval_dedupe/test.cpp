#include "framework/expect.hpp"
#include "mod/tests/network/_shared/helpers.hpp"
#include "network/alephium/http_io_pool.hpp"

void test_interval_dedupe(VnvStats& s)
{
    std::printf("\n== interval dedupe / inflight ==\n");
    network_test_fake() = std::make_shared<FakeHttpTransport>();
    network_test_fake()->set_delay_ms(50);
    network_test_fake()->set_default_ok_body("{}");

    HttpIoPool pool;
    pool.start("http://test.local", 2, network_test_factory());
    VNV_EXPECT(s, pool.enqueue_interval(5000, 6000, false));
    VNV_EXPECT(s, !pool.enqueue_interval(5000, 6000, false)); // inflight
    wait_inflight_zero(pool);
    pool.drain_results(8);
    VNV_EXPECT(s, !pool.enqueue_interval(5000, 6000, false)); // completed
    VNV_EXPECT(s, pool.enqueue_interval(5000, 6000, true));   // force
    wait_inflight_zero(pool);
    pool.stop();
}
