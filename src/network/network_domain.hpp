#pragma once

// Network domain selection (mainnet / testnet / debug stub).
// Debug is reserved for a future offline simulator — not selectable yet.

#include <cstdint>
#include <cstring>
#include <string>

enum class NetworkDomain : uint8_t
{
    Mainnet = 0,
    Testnet = 1,
    Debug   = 2,
};

inline constexpr const char* network_domain_label(NetworkDomain d)
{
    switch (d)
    {
    case NetworkDomain::Mainnet: return "Mainnet";
    case NetworkDomain::Testnet: return "Testnet";
    case NetworkDomain::Debug:   return "Debug";
    }
    return "Mainnet";
}

inline constexpr const char* network_domain_default_url(NetworkDomain d)
{
    switch (d)
    {
    case NetworkDomain::Mainnet: return "https://node.mainnet.alephium.org";
    case NetworkDomain::Testnet: return "https://node.testnet.alephium.org";
    case NetworkDomain::Debug:   return "";
    }
    return "https://node.mainnet.alephium.org";
}

// Explorer host for block/tx links (Block panel).
inline constexpr const char* network_domain_explorer_host(NetworkDomain d)
{
    switch (d)
    {
    case NetworkDomain::Testnet: return "https://testnet.alephium.org";
    case NetworkDomain::Mainnet:
    default:                     return "https://explorer.alephium.org";
    }
}

// Match config.json URL to a domain (substring heuristics).
inline NetworkDomain network_domain_from_url(const char* url)
{
    if (!url)
        return NetworkDomain::Mainnet;
    if (std::strstr(url, "testnet"))
        return NetworkDomain::Testnet;
    if (std::strstr(url, "mainnet"))
        return NetworkDomain::Mainnet;
    return NetworkDomain::Mainnet;
}

// Prefer config entry containing domain token; else hard-coded default.
inline std::string network_domain_resolve_url(NetworkDomain d, const char* const* urls, int count)
{
    if (d == NetworkDomain::Debug)
        return {};
    const char* token = (d == NetworkDomain::Testnet) ? "testnet" : "mainnet";
    if (urls)
    {
        for (int i = 0; i < count; ++i)
        {
            if (urls[i] && std::strstr(urls[i], token))
                return urls[i];
        }
    }
    return network_domain_default_url(d);
}

// Network status for left-rail HUD (int for UiSnapshot).
enum class NetworkStatus : int
{
    Idle         = 0,
    Connecting   = 1,
    Bootstrapping = 2,
    IdentifyTips = 3,
    ConfirmWalk  = 4,
    Steady       = 5,
    Switching    = 6,
    Error        = 7,
};

inline constexpr const char* network_status_label(NetworkStatus s)
{
    switch (s)
    {
    case NetworkStatus::Idle:          return "Idle";
    case NetworkStatus::Connecting:    return "Connecting";
    case NetworkStatus::Bootstrapping: return "Bootstrapping";
    case NetworkStatus::IdentifyTips:  return "Identify tips";
    case NetworkStatus::ConfirmWalk:   return "Confirm walk";
    case NetworkStatus::Steady:        return "Steady";
    case NetworkStatus::Switching:     return "Switching…";
    case NetworkStatus::Error:         return "Error";
    }
    return "Idle";
}
