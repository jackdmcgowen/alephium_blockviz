#pragma once

// Main-chain admission gate for the visualizer poll path.
// Positive results are cached permanently; negatives are not (retry as tip advances).
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "alph_block.hpp"

#ifndef ALPH_MAIN_CHAIN_CONFIRM_DEPTH
#define ALPH_MAIN_CHAIN_CONFIRM_DEPTH 2
#endif

class MainChainCache
{
public:
    // Refresh tip heights for all 16 chains (HTTP). Call from poll thread only.
    void refresh_tips();

    int tip(int from_group, int to_group) const;

    // True if hash is known main-chain. Uses hashes singleton fast path, then is-block-in-main-chain.
    // from/to/height enable bulk-friendly checks; pass -1 height to force direct is-main call only.
    bool ensure(const std::string& hash, int from_group, int to_group, int height);

    bool is_cached_main(const std::string& hash) const;

private:
    bool query_is_main(const std::string& hash);
    bool try_hashes_singleton(const std::string& hash, int from_group, int to_group, int height);

    std::unordered_set<std::string> main_yes_; // permanent positives only
    int tips_[ALPH_NUM_GROUPS][ALPH_NUM_GROUPS]{};
    bool tips_valid_ = false;
};
