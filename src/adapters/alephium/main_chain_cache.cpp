#include "adapters/alephium/main_chain_cache.hpp"

#include <cjson/cJSON.h>
#include <cstdio>
#include <cstring>

#include "commands.h"

void MainChainCache::refresh_tips()
{
    for (int f = 0; f < ALPH_NUM_GROUPS; ++f)
    {
        for (int t = 0; t < ALPH_NUM_GROUPS; ++t)
            tips_[f][t] = get_height(f, t);
    }
    tips_valid_ = true;
}

int MainChainCache::tip(int from_group, int to_group) const
{
    if (from_group < 0 || from_group >= ALPH_NUM_GROUPS ||
        to_group < 0 || to_group >= ALPH_NUM_GROUPS)
        return -1;
    return tips_[from_group][to_group];
}

bool MainChainCache::is_cached_main(const std::string& hash) const
{
    return main_yes_.find(hash) != main_yes_.end();
}

bool MainChainCache::query_is_main(const std::string& hash)
{
    int transport_ok = 0;
    const int is_main = get_blockflow_is_block_in_main_chain(hash.c_str(), &transport_ok);
    if (!transport_ok)
        return false;
    if (is_main)
        main_yes_.insert(hash);
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
            // Sole hash at height — treat as main (cheap bulk path)
            main_yes_.insert(hash);
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

    if (main_yes_.count(hash))
        return true;

    // Deep-zone cheap path: single hash at height → accept without is-main RTT
    if (try_hashes_singleton(hash, from_group, to_group, height))
        return true;

    // Hot zone / multi-hash heights / singleton miss: definitive check
    return query_is_main(hash);
}
