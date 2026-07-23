#pragma once

// Lightweight prefs persistence (domain + filters). Separate from config.json
// endpoint list so URL tables stay human-editable.

#include "network/network_domain.hpp"

#include <cjson/cJSON.h>
#include <cstdio>
#include <cstring>
#include <string>

struct UserPrefs
{
    NetworkDomain domain         = NetworkDomain::Mainnet;
    bool          filter_multi_tx = false;
    // Min block output ALPH (human); 0 = filter off.
    double        filter_min_alph = 0.0;
    bool          filter_unconfirmed_only = false;
    // 0 = use code default lookback; >0 overrides seconds.
    int           lookback_seconds = 0;
};

inline const char* kUserPrefsPath = "user_prefs.json";

inline NetworkDomain prefs_domain_from_string(const char* s)
{
    if (!s)
        return NetworkDomain::Mainnet;
    if (std::strcmp(s, "testnet") == 0)
        return NetworkDomain::Testnet;
    if (std::strcmp(s, "debug") == 0)
        return NetworkDomain::Debug;
    return NetworkDomain::Mainnet;
}

inline const char* prefs_domain_to_string(NetworkDomain d)
{
    switch (d)
    {
    case NetworkDomain::Testnet: return "testnet";
    case NetworkDomain::Debug:   return "debug";
    case NetworkDomain::Mainnet:
    default:                     return "mainnet";
    }
}

inline UserPrefs load_user_prefs(const char* path = kUserPrefsPath)
{
    UserPrefs p;
    FILE* f = std::fopen(path, "rb");
    if (!f)
        return p;
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 1'000'000)
    {
        std::fclose(f);
        return p;
    }
    std::string buf(static_cast<size_t>(sz), '\0');
    const size_t n = std::fread(buf.data(), 1, static_cast<size_t>(sz), f);
    std::fclose(f);
    if (n == 0)
        return p;
    buf.resize(n);

    cJSON* root = cJSON_Parse(buf.c_str());
    if (!root)
        return p;
    if (const cJSON* d = cJSON_GetObjectItem(root, "domain"))
    {
        if (cJSON_IsString(d) && d->valuestring)
            p.domain = prefs_domain_from_string(d->valuestring);
        else if (cJSON_IsNumber(d))
            p.domain = static_cast<NetworkDomain>(d->valueint);
    }
    if (const cJSON* m = cJSON_GetObjectItem(root, "filter_multi_tx"))
    {
        if (cJSON_IsBool(m))
            p.filter_multi_tx = cJSON_IsTrue(m);
    }
    if (const cJSON* a = cJSON_GetObjectItem(root, "filter_min_alph"))
    {
        if (cJSON_IsNumber(a) && a->valuedouble > 0.0)
            p.filter_min_alph = a->valuedouble;
    }
    if (const cJSON* u = cJSON_GetObjectItem(root, "filter_unconfirmed_only"))
    {
        if (cJSON_IsBool(u))
            p.filter_unconfirmed_only = cJSON_IsTrue(u);
    }
    if (const cJSON* lb = cJSON_GetObjectItem(root, "lookback_seconds"))
    {
        if (cJSON_IsNumber(lb) && lb->valueint > 0)
            p.lookback_seconds = lb->valueint;
    }
    cJSON_Delete(root);
    return p;
}

inline bool save_user_prefs(const UserPrefs& p, const char* path = kUserPrefsPath)
{
    cJSON* root = cJSON_CreateObject();
    if (!root)
        return false;
    cJSON_AddStringToObject(root, "domain", prefs_domain_to_string(p.domain));
    cJSON_AddBoolToObject(root, "filter_multi_tx", p.filter_multi_tx ? 1 : 0);
    if (p.filter_min_alph > 0.0)
        cJSON_AddNumberToObject(root, "filter_min_alph", p.filter_min_alph);
    cJSON_AddBoolToObject(root, "filter_unconfirmed_only",
                          p.filter_unconfirmed_only ? 1 : 0);
    if (p.lookback_seconds > 0)
        cJSON_AddNumberToObject(root, "lookback_seconds", p.lookback_seconds);

    char* printed = cJSON_Print(root);
    cJSON_Delete(root);
    if (!printed)
        return false;

    FILE* f = std::fopen(path, "wb");
    if (!f)
    {
        cJSON_free(printed);
        return false;
    }
    const size_t len = std::strlen(printed);
    const bool ok = std::fwrite(printed, 1, len, f) == len;
    std::fclose(f);
    cJSON_free(printed);
    return ok;
}
