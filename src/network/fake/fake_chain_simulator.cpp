#include "network/pch.h"
#include "network/fake/fake_chain_simulator.hpp"
#include "network/network_domain.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>

std::atomic<int> FakeChainSimulator::bootstrap_heights_override_{ 0 };

void FakeChainSimulator::set_bootstrap_heights_override(int heights)
{
    bootstrap_heights_override_.store(heights > 0 ? heights : 0);
}

int FakeChainSimulator::bootstrap_heights_override()
{
    return bootstrap_heights_override_.load();
}

int FakeChainSimulator::bootstrap_heights_() const
{
    const int o = bootstrap_heights_override_.load();
    return o > 0 ? o : kBootstrapHeights;
}

FakeChainSimulator::FakeChainSimulator(BlockScene& scene)
    : scene_(scene)
{
    for (int i = 0; i < BlockScene::kLaneCount; ++i)
    {
        tip_height_[i] = -1;
        tip_hash_[i].clear();
    }
}

FakeChainSimulator::~FakeChainSimulator()
{
    stop();
}

void FakeChainSimulator::start()
{
    if (running_.load())
        return;
    stop_.store(false);
    running_.store(true);
    thread_ = std::thread([this] { thread_main_(); });
    std::printf("[fake] FakeChainSimulator started (Debug domain)\n");
}

void FakeChainSimulator::stop()
{
    if (!running_.load())
        return;
    stop_.store(true);
    if (thread_.joinable())
        thread_.join();
    running_.store(false);
    std::printf("[fake] FakeChainSimulator stopped\n");
}

std::string FakeChainSimulator::make_hash_(uint32_t lane, int height, uint32_t salt)
{
    // Deterministic 64-char hex-looking id (not crypto; unique for viz/pick).
    char buf[65];
    std::snprintf(buf, sizeof(buf),
                  "dbg%02x%08x%08x%08x%08x%08x%08x%04x",
                  static_cast<unsigned>(lane & 0xff),
                  static_cast<unsigned>(height),
                  salt,
                  0xA11E0000u ^ (lane * 0x10001u),
                  0xB10C0000u ^ static_cast<unsigned>(height * 17),
                  0xC0FFEEu ^ salt,
                  0xDEADBEEFu,
                  static_cast<unsigned>((lane + height + salt) & 0xffff));
    return std::string(buf);
}

AlphBlock FakeChainSimulator::make_block_(uint32_t lane, int height, int64_t ts_ms,
                                          const std::string& parent_hash, int txn_count)
{
    AlphBlock b;
    b.chainFrom = static_cast<uint8_t>(lane / ALPH_NUM_GROUPS);
    b.chainTo   = static_cast<uint8_t>(lane % ALPH_NUM_GROUPS);
    b.height    = height;
    b.timestamp = ts_ms;
    b.hash      = make_hash_(lane, height);
    b.txn_count = txn_count;
    if (!parent_hash.empty())
        b.deps.push_back(parent_hash);
    // Optional dummy txn so slim/inspector paths have something to work with.
    if (txn_count > 0)
    {
        AlphTxn t;
        t.txid = b.hash + "_tx0";
        t.version = 1;
        t.networkId = 0;
        t.gasAmount = 20000;
        t.gasPrice = "100000000000";
        b.txns.push_back(std::move(t));
    }
    return b;
}

void FakeChainSimulator::publish_hud_(int status)
{
    BlockScene::NetworkHud hud = scene_.network_hud();
    hud.domain = static_cast<int>(NetworkDomain::Debug);
    hud.status = status;
    hud.switching = 0;
    std::snprintf(hud.base_url, sizeof(hud.base_url), "debug://fake-chain");
    hud.lookback_windows_need = 1;
    hud.lookback_windows_done = 1;
    hud.poll_interval_sec = static_cast<float>(kTickMs) / 1000.f;
    hud.last_poll_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    int lanes = 0;
    for (int i = 0; i < BlockScene::kLaneCount; ++i)
    {
        hud.tip_height_by_lane[i] = tip_height_[i];
        if (tip_height_[i] >= 0)
            ++lanes;
    }
    hud.lanes_with_frontier = lanes;
    hud.open_confirm_walks = 0;
    hud.stats_seed_q = 0;
    scene_.set_network_hud(hud);
}

void FakeChainSimulator::bootstrap_()
{
    const int heights = bootstrap_heights_();
    genesis_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count() -
                  static_cast<int64_t>(heights) * ALPH_TARGET_BLOCK_SECONDS * 1000;
    scene_.set_genesis_ms(genesis_ms_);
    scene_.set_timeline_origin_ms(genesis_ms_);

    publish_hud_(static_cast<int>(NetworkStatus::Bootstrapping));

    for (int h = 0; h < heights; ++h)
    {
        if (stop_.load())
            return;
        const int64_t ts = genesis_ms_ + static_cast<int64_t>(h) * ALPH_TARGET_BLOCK_SECONDS * 1000;
        for (uint32_t lane = 0; lane < static_cast<uint32_t>(BlockScene::kLaneCount); ++lane)
        {
            const std::string parent = (h > 0) ? tip_hash_[lane] : std::string{};
            const int txn_n = (h % 3 == 0) ? 2 : 1; // some multi-tx for filter testing
            AlphBlock blk = make_block_(lane, h, ts, parent, txn_n);
            scene_.add_block(blk);
            tip_height_[lane] = h;
            tip_hash_[lane] = blk.hash;
            // Sequential confirm at each height (simulates main-chain tip advance).
            scene_.mark_confirmed(blk.hash, lane, h, /*chain_walk=*/false);
        }
    }

    // Dense benches: raise soft node cap so prune does not wipe bootstrap immediately.
    if (heights > kBootstrapHeights)
        scene_.prune(0, /*max_nodes=*/static_cast<size_t>(heights) * BlockScene::kLaneCount + 256);

    publish_hud_(static_cast<int>(NetworkStatus::Steady));
    std::printf("[fake] bootstrap complete: %d heights x 16 lanes\n", heights);
}

void FakeChainSimulator::tick_grow_()
{
    ++tick_;
    // Advance one lane per tick (round-robin) so growth is visible.
    const uint32_t lane = static_cast<uint32_t>(tick_ % BlockScene::kLaneCount);
    const int next_h = tip_height_[lane] + 1;
    const int64_t ts = genesis_ms_ + static_cast<int64_t>(next_h) * ALPH_TARGET_BLOCK_SECONDS * 1000;
    const int txn_n = (next_h % 4 == 0) ? 3 : 1;
    AlphBlock blk = make_block_(lane, next_h, ts, tip_hash_[lane], txn_n);
    if (scene_.add_block(blk))
    {
        tip_height_[lane] = next_h;
        tip_hash_[lane] = blk.hash;
        scene_.mark_confirmed(blk.hash, lane, next_h, /*chain_walk=*/false);
    }
    // Every 16 ticks, inject a short-lived "incomplete" block on lane 0 that is
    // not confirmed (deps a fake missing hash) — orange path practice.
    if (tick_ % 32 == 0)
    {
        AlphBlock ghost;
        ghost.chainFrom = 0;
        ghost.chainTo = 0;
        ghost.height = tip_height_[0] + 1;
        ghost.timestamp = ts + 1;
        ghost.hash = make_hash_(0, ghost.height, /*salt=*/0xBAD);
        ghost.deps.push_back("missing_dep_for_orange_viz");
        ghost.txn_count = 1;
        scene_.add_block(ghost);
        // Leave unconfirmed — presenter may classify incomplete if deps missing.
    }
    // Soft retention: keep last ~64 heights worth of timeline + node cap.
    if (tick_ % 20 == 0)
    {
        const int64_t min_ts = ts - static_cast<int64_t>(64) * ALPH_TARGET_BLOCK_SECONDS * 1000;
        scene_.prune(min_ts, /*max_nodes=*/4000);
    }
    publish_hud_(static_cast<int>(NetworkStatus::Steady));
}

void FakeChainSimulator::thread_main_()
{
    bootstrap_();
    while (!stop_.load())
    {
        tick_grow_();
        for (int i = 0; i < kTickMs / 50 && !stop_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
