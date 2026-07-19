#include "framework/expect.hpp"
#include "mod/tests/network/_shared/helpers.hpp"
#include "network/alephium/http_io_pool.hpp"

void test_enqueue_block_and_drain(VnvStats& s)
{
    std::printf("\n== enqueue block hash + drain ==\n");
    network_test_fake() = std::make_shared<FakeHttpTransport>();
    network_test_fake()->set_default_ok_body(R"({"hash":"abc"})");

    HttpIoPool pool;
    pool.start("http://test.local", 2, network_test_factory());
    VNV_EXPECT(s, pool.enqueue_block_hash("abc"));
    wait_inflight_zero(pool);
    auto res = pool.drain_results(8);
    VNV_EXPECT(s, res.size() == 1);
    VNV_EXPECT(s, res[0].ok);
    VNV_EXPECT(s, res[0].kind == HttpIoPool::Kind::BlockHash);
    VNV_EXPECT(s, res[0].hash == "abc");
    VNV_EXPECT(s, network_test_fake()->call_count() >= 1);
    pool.stop();
}
