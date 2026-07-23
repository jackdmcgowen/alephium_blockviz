#pragma once

// Offline Debug domain: synthetic 16-lane BlockFlow without HTTP.
// Thread-owned; NetworkSystem starts/stops it instead of NetworkPoller when domain=Debug.

#include "domain/block_scene.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
// std::atomic used for bootstrap_heights_override_

class FakeChainSimulator
{
public:
    explicit FakeChainSimulator(BlockScene& scene);
    ~FakeChainSimulator();

    FakeChainSimulator(const FakeChainSimulator&) = delete;
    FakeChainSimulator& operator=(const FakeChainSimulator&) = delete;

    void start();
    void stop();

    bool running() const { return running_.load(); }

    // Bench/tests only: denser history before start(). 0 = use default kBootstrapHeights.
    // Call before NetworkSystem/FakeChain start (before engine->start()).
    static void set_bootstrap_heights_override(int heights);
    static int  bootstrap_heights_override();

private:
    void thread_main_();
    void bootstrap_();
    void tick_grow_();
    void publish_hud_(int status);
    int  bootstrap_heights_() const;

    static std::string make_hash_(uint32_t lane, int height, uint32_t salt = 0);
    static AlphBlock make_block_(uint32_t lane, int height, int64_t ts_ms,
                                 const std::string& parent_hash, int txn_count);

    BlockScene& scene_;
    std::thread thread_;
    std::atomic<bool> running_{ false };
    std::atomic<bool> stop_{ false };

    // Per-lane tip height (confirmed at tip after bootstrap).
    int tip_height_[BlockScene::kLaneCount]{};
    std::string tip_hash_[BlockScene::kLaneCount]{};

    int64_t genesis_ms_ = 0;
    int     tick_       = 0;

    static constexpr int kBootstrapHeights = 8;
    static constexpr int kTickMs           = 500;
    static std::atomic<int> bootstrap_heights_override_;
};
