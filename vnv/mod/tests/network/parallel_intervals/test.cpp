#include "framework/expect.hpp"
#include "mod/tests/network/_shared/helpers.hpp"
#include "network/alephium/http_io_pool.hpp"

void test_parallel_intervals(VnvStats& s)
{
    std::printf("\n== parallel interval jobs ==\n");
    network_test_fake() = std::make_shared<FakeHttpTransport>();
    network_test_fake()->set_delay_ms(80);
    network_test_fake()->set_default_ok_body(R"({"blocksAndEvents":[]})");

    HttpIoPool pool;
    pool.set_max_inflight_intervals(4);
    pool.start("http://test.local", 4, network_test_factory());

    VNV_EXPECT(s, pool.enqueue_interval(1000, 2000, false));
    VNV_EXPECT(s, pool.enqueue_interval(2000, 3000, false));
    VNV_EXPECT(s, pool.enqueue_interval(3000, 4000, false));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const int max_c = network_test_fake()->max_concurrent();
    wait_inflight_zero(pool);
    auto res = pool.drain_results(16);
    VNV_EXPECT(s, res.size() == 3);
    VNV_EXPECT(s, max_c >= 2);
    for (const auto& r : res)
        VNV_EXPECT(s, r.kind == HttpIoPool::Kind::Interval && r.ok);
    pool.stop();
}
