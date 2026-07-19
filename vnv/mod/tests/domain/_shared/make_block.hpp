#pragma once

#include "domain/alph_block.hpp"

#include <string>

inline AlphBlock make_block(const char* hash, int from, int to, int height, int txn_n = 1)
{
    AlphBlock b;
    b.hash = hash;
    b.chainFrom = static_cast<uint8_t>(from);
    b.chainTo = static_cast<uint8_t>(to);
    b.height = height;
    b.timestamp = 1'000'000 + height * 8000;
    b.txn_count = txn_n;
    for (int i = 0; i < txn_n; ++i)
    {
        AlphTxn t;
        t.txid = std::string(hash) + "_tx" + std::to_string(i);
        t.gasAmount = 1;
        t.gasPrice = "1";
        b.txns.push_back(std::move(t));
    }
    return b;
}
