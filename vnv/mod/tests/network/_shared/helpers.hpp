#pragma once

#include "mod/tests/network/_shared/fake_http_transport.hpp"
#include "network/alephium/http_io_pool.hpp"

#include <chrono>
#include <memory>
#include <thread>

inline std::shared_ptr<FakeHttpTransport>& network_test_fake()
{
    static std::shared_ptr<FakeHttpTransport> g_fake;
    return g_fake;
}

inline HttpIoPool::TransportFactory network_test_factory()
{
    return []() -> std::shared_ptr<IHttpTransport> { return network_test_fake(); };
}

inline void wait_inflight_zero(HttpIoPool& pool, int timeout_ms = 5000)
{
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (pool.in_flight() > 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
}
