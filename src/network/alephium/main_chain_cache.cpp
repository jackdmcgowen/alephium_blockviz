#include "network/pch.h"
#include "network/alephium/main_chain_cache.hpp"

#include <cjson/cJSON.h>
#include <cstdio>

#include "network/commands.h"

void MainChainCache::refresh_tips()
{
    for (int f = 0; f < ALPH_NUM_GROUPS; ++f)
    {
        for (int t = 0; t < ALPH_NUM_GROUPS; ++t)
            tips_[f][t] = get_height(f, t);
    }
    tips_valid_ = true;
}

void MainChainCache::clear()
{
    main_yes_.clear();
    tips_valid_ = false;
    for (int f = 0; f < ALPH_NUM_GROUPS; ++f)
        for (int t = 0; t < ALPH_NUM_GROUPS; ++t)
            tips_[f][t] = -1;
}

int MainChainCache::tip(int from_group, int to_group) const
{
    if (from_group < 0 || from_group >= ALPH_NUM_GROUPS ||
        to_group < 0 || to_group >= ALPH_NUM_GROUPS)
        return -1;
    return tips_[from_group][to_group];
}

bool MainChainCache::is_hot_zone(int from_group, int to_group, int height) const
{
    if (!tips_valid_)
        return true;
    const int t = tip(from_group, to_group);
    if (t < 0)
        return true;
    // height + D > tip  => near tip
    return height + ALPH_MAIN_CHAIN_CONFIRM_DEPTH > t;
}

bool MainChainCache::is_cached_main(const std::string& hash) const
{
    return main_yes_.find(hash) != main_yes_.end();
}

void MainChainCache::mark_main(const std::string& hash)
{
    if (!hash.empty())
        main_yes_.insert(hash);
}

bool MainChainCache::query_is_main(const std::string& hash, bool* transport_ok)
{
    int ok = 0;
    const int is_main = get_blockflow_is_block_in_main_chain(hash.c_str(), &ok);
    if (transport_ok)
        *transport_ok = (ok != 0);
    if (!ok)
        return false;
    if (is_main)
        mark_main(hash);
    return is_main != 0;
}

bool MainChainCache::try_hashes_singleton(const std::string& hash, int from_group, int to_group, int height)
{
    if (height < 0 || from_group < 0 || to_group < 0)
        return false;

    cJSON* arr = get_blockflow_hashes(from_group, to_group, height);
    if (!arr)
        return false;

    bool accepted = false;
    if (cJSON_IsArray(arr) && cJSON_GetArraySize(arr) == 1)
    {
        cJSON* item = cJSON_GetArrayItem(arr, 0);
        if (item && cJSON_IsString(item) && item->valuestring && hash == item->valuestring)
        {
            mark_main(hash);
            accepted = true;
        }
    }
    cJSON_Delete(arr);
    return accepted;
}

bool MainChainCache::ensure(const std::string& hash, int from_group, int to_group, int height)
{
    if (hash.empty())
        return false;
    if (is_cached_main(hash))
        return true;
    if (try_hashes_singleton(hash, from_group, to_group, height))
        return true;
    return query_is_main(hash);
}
