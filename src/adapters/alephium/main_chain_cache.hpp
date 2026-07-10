#pragma once

// Main-chain cache + helpers. Network thread only.
// Positives cached permanently; negatives are not (retry).
#include <string>
#include <unordered_set>

#include "alph_block.hpp"

#ifndef ALPH_MAIN_CHAIN_CONFIRM_DEPTH
#define ALPH_MAIN_CHAIN_CONFIRM_DEPTH 2
#endif

class MainChainCache
{
public:
    void refresh_tips();
    int tip(int from_group, int to_group) const;
    bool tips_valid() const { return tips_valid_; }

    bool is_cached_main(const std::string& hash) const;
    void mark_main(const std::string& hash);

    // Definitive API check; caches true on success. transport_ok optional.
    bool query_is_main(const std::string& hash, bool* transport_ok = nullptr);

    // If exactly one hash at height and it matches, mark main and return true.
    bool try_hashes_singleton(const std::string& hash, int from_group, int to_group, int height);

    // Legacy blocking ensure (prefer optimistic + background verify).
    bool ensure(const std::string& hash, int from_group, int to_group, int height);

    // Hot zone if tip unknown or height is within D of tip.
    bool is_hot_zone(int from_group, int to_group, int height) const;

private:
    std::unordered_set<std::string> main_yes_;
    int tips_[ALPH_NUM_GROUPS][ALPH_NUM_GROUPS]{};
    bool tips_valid_ = false;
};
