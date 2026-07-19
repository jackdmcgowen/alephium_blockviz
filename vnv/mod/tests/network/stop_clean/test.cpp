#include "framework/expect.hpp"
#include "mod/tests/network/_shared/helpers.hpp"
#include "network/alephium/http_io_pool.hpp"

void test_stop_clean(VnvStats& s)
{
    std::printf("\n== stop joins clean ==\n");
    network_test_fake() = std::make_shared<FakeHttpTransport>();
    network_test_fake()->set_delay_ms(30);
    network_test_fake()->set_default_ok_body("{}");

    HttpIoPool pool;
    pool.start("http://test.local", 3, network_test_factory());
    pool.enqueue_interval(1, 2, false);
    pool.enqueue_block_hash("x");
    pool.stop();
    VNV_EXPECT(s, pool.in_flight() == 0);
    VNV_EXPECT(s, pool.pending_jobs() == 0);
}
