#include "network/pch.h"
#include "network/alephium/segment_disk_cache.hpp"
#include "network/network_domain.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace
{
std::string local_app_data_()
{
#if defined(_WIN32)
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, "LOCALAPPDATA") == 0 && buf && buf[0])
    {
        std::string s(buf);
        free(buf);
        return s;
    }
    if (buf)
        free(buf);
#endif
    return ".";
}

std::string hash_prefix2_(const std::string& hash)
{
    if (hash.size() >= 2)
        return hash.substr(0, 2);
    return "00";
}
} // namespace

SegmentDiskCache::SegmentDiskCache(std::string domain_key)
{
    set_domain(std::move(domain_key));
}

void SegmentDiskCache::set_domain(std::string domain_key)
{
    domain_key_ = std::move(domain_key);
    for (char& c : domain_key_)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        if (c == ' ' || c == '/' || c == '\\')
            c = '_';
    }
}

std::string SegmentDiskCache::domain_key_from_int(int domain)
{
    switch (static_cast<NetworkDomain>(domain))
    {
    case NetworkDomain::Testnet: return "testnet";
    case NetworkDomain::Debug:   return "debug";
    case NetworkDomain::Mainnet:
    default:                     return "mainnet";
    }
}

std::vector<int64_t> SegmentDiskCache::chunk_keys_for_window(int64_t from_ms, int64_t to_ms)
{
    std::vector<int64_t> keys;
    if (to_ms <= from_ms || kChunkMs <= 0)
        return keys;
    for (int64_t t = to_ms; t > from_ms;)
    {
        const int64_t chunk_to = t;
        const int64_t chunk_from =
            (from_ms > chunk_to - kChunkMs) ? from_ms : (chunk_to - kChunkMs);
        keys.push_back(chunk_from);
        if (chunk_from <= from_ms)
            break;
        t = chunk_from;
    }
    return keys;
}

std::string SegmentDiskCache::root_dir_() const
{
    return local_app_data_() + "\\AlephiumBlockViz\\cache\\" + domain_key_;
}

std::string SegmentDiskCache::manifest_path_() const
{
    return root_dir_() + "\\manifest.json";
}

std::string SegmentDiskCache::segment_path_(int g_seg) const
{
    return root_dir_() + "\\segments\\G_" + std::to_string(g_seg) + ".json";
}

std::string SegmentDiskCache::block_path_(const std::string& hash) const
{
    return root_dir_() + "\\blocks\\" + hash_prefix2_(hash) + "\\" + hash + ".json.gz";
}

bool SegmentDiskCache::ensure_dirs_() const
{
    if (!enabled())
        return false;
    std::error_code ec;
    fs::create_directories(root_dir_() + "\\segments", ec);
    fs::create_directories(root_dir_() + "\\blocks", ec);
    return !ec;
}

bool SegmentDiskCache::write_text_file_(const std::string& path, const std::string& body) const
{
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    return static_cast<bool>(out);
}

bool SegmentDiskCache::read_text_file_(const std::string& path, std::string& out) const
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool SegmentDiskCache::write_gzip_file_(const std::string& path, const std::string& body) const
{
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    gzFile gz = gzopen(path.c_str(), "wb");
    if (!gz)
        return false;
    const int n = static_cast<int>(body.size());
    const int w = gzwrite(gz, body.data(), static_cast<unsigned>(n));
    gzclose(gz);
    return w == n;
}

bool SegmentDiskCache::read_gzip_file_(const std::string& path, std::string& out) const
{
    gzFile gz = gzopen(path.c_str(), "rb");
    if (!gz)
        return false;
    out.clear();
    char buf[8192];
    int n = 0;
    while ((n = gzread(gz, buf, sizeof(buf))) > 0)
        out.append(buf, static_cast<size_t>(n));
    const int err = gzclose(gz);
    return err == Z_OK || err == Z_STREAM_END || n >= 0;
}

std::string SegmentDiskCache::block_to_json_(const CachedBlock& b)
{
    cJSON* root = cJSON_CreateObject();
    if (!root)
        return {};
    cJSON_AddStringToObject(root, "hash", b.block.hash.c_str());
    cJSON_AddNumberToObject(root, "chainFrom", b.block.chainFrom);
    cJSON_AddNumberToObject(root, "chainTo", b.block.chainTo);
    cJSON_AddNumberToObject(root, "height", b.block.height);
    cJSON_AddNumberToObject(root, "timestamp", static_cast<double>(b.block.timestamp));
    cJSON_AddNumberToObject(root, "txn_count", b.block.txn_count);
    if (!b.block.alph_out_atto.empty())
        cJSON_AddStringToObject(root, "alph_out_atto", b.block.alph_out_atto.c_str());
    cJSON_AddBoolToObject(root, "confirmed", b.confirmed ? 1 : 0);

    cJSON* deps = cJSON_AddArrayToObject(root, "deps");
    for (const std::string& d : b.block.deps)
        cJSON_AddItemToArray(deps, cJSON_CreateString(d.c_str()));

    cJSON* uncles = cJSON_AddArrayToObject(root, "ghostUncles");
    for (const std::string& u : b.block.uncles)
    {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "blockHash", u.c_str());
        cJSON_AddItemToArray(uncles, o);
    }
    // Empty transactions so AlphBlock ctor paths stay happy if reused.
    cJSON_AddArrayToObject(root, "transactions");

    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed)
        return {};
    std::string s(printed);
    cJSON_free(printed);
    return s;
}

bool SegmentDiskCache::json_to_block_(const std::string& json, CachedBlock& out)
{
    cJSON* root = cJSON_Parse(json.c_str());
    if (!root)
        return false;
    AlphBlock b(root);
    if (b.hash.empty())
    {
        cJSON_Delete(root);
        return false;
    }
    if (cJSON* tc = cJSON_GetObjectItem(root, "txn_count"))
        b.txn_count = tc->valueint;
    if (cJSON* ao = cJSON_GetObjectItem(root, "alph_out_atto"); ao && cJSON_IsString(ao))
        b.alph_out_atto = ao->valuestring ? ao->valuestring : "";
    out.block = std::move(b);
    out.confirmed = cJSON_IsTrue(cJSON_GetObjectItem(root, "confirmed"));
    cJSON_Delete(root);
    return true;
}

bool SegmentDiskCache::load_manifest_(std::vector<SegmentMeta>& segs, int64_t& genesis_ms) const
{
    segs.clear();
    genesis_ms = 0;
    std::string body;
    if (!read_text_file_(manifest_path_(), body))
        return false;
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root)
        return false;
    const cJSON* ver = cJSON_GetObjectItem(root, "schema_version");
    if (!ver || ver->valueint != kSchemaVersion)
    {
        cJSON_Delete(root);
        return false;
    }
    if (cJSON* g = cJSON_GetObjectItem(root, "genesis_ms"))
        genesis_ms = static_cast<int64_t>(g->valuedouble);
    cJSON* arr = cJSON_GetObjectItem(root, "segments");
    if (cJSON_IsArray(arr))
    {
        cJSON* it = nullptr;
        cJSON_ArrayForEach(it, arr)
        {
            SegmentMeta m;
            if (cJSON* x = cJSON_GetObjectItem(it, "g_seg"))
                m.g_seg = x->valueint;
            if (cJSON* x = cJSON_GetObjectItem(it, "from_ms"))
                m.from_ms = static_cast<int64_t>(x->valuedouble);
            if (cJSON* x = cJSON_GetObjectItem(it, "to_ms"))
                m.to_ms = static_cast<int64_t>(x->valuedouble);
            if (cJSON* x = cJSON_GetObjectItem(it, "verified_at"))
                m.verified_at = static_cast<int64_t>(x->valuedouble);
            if (cJSON* x = cJSON_GetObjectItem(it, "block_count"))
                m.block_count = x->valueint;
            if (m.g_seg >= 0 && m.to_ms > m.from_ms)
                segs.push_back(m);
        }
    }
    cJSON_Delete(root);
    return true;
}

bool SegmentDiskCache::save_manifest_(const std::vector<SegmentMeta>& segs,
                                      int64_t genesis_ms) const
{
    cJSON* root = cJSON_CreateObject();
    if (!root)
        return false;
    cJSON_AddNumberToObject(root, "schema_version", kSchemaVersion);
    cJSON_AddStringToObject(root, "domain", domain_key_.c_str());
    cJSON_AddNumberToObject(root, "genesis_ms", static_cast<double>(genesis_ms));
    cJSON* arr = cJSON_AddArrayToObject(root, "segments");
    for (const SegmentMeta& m : segs)
    {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "g_seg", m.g_seg);
        cJSON_AddNumberToObject(o, "from_ms", static_cast<double>(m.from_ms));
        cJSON_AddNumberToObject(o, "to_ms", static_cast<double>(m.to_ms));
        cJSON_AddNumberToObject(o, "verified_at", static_cast<double>(m.verified_at));
        cJSON_AddNumberToObject(o, "block_count", m.block_count);
        cJSON_AddItemToArray(arr, o);
    }
    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed)
        return false;
    const bool ok = write_text_file_(manifest_path_(), printed);
    cJSON_free(printed);
    return ok;
}

void SegmentDiskCache::enforce_lru_(std::vector<SegmentMeta>& segs) const
{
    if (static_cast<int>(segs.size()) <= kMaxSegments)
        return;
    // Keep highest g_seg (most recent toward live).
    std::sort(segs.begin(), segs.end(),
              [](const SegmentMeta& a, const SegmentMeta& b) { return a.g_seg > b.g_seg; });
    while (static_cast<int>(segs.size()) > kMaxSegments)
    {
        const SegmentMeta drop = segs.back();
        segs.pop_back();
        std::error_code ec;
        fs::remove(segment_path_(drop.g_seg), ec);
        // Leave block files; orphaned blocks ok until manual clean (size-bound later).
    }
}

bool SegmentDiskCache::save_segment(int g_seg, int64_t from_ms, int64_t to_ms,
                                    int64_t genesis_ms,
                                    const std::vector<CachedBlock>& blocks)
{
    if (!enabled() || g_seg < 0 || to_ms <= from_ms || blocks.empty())
        return false;
    if (!ensure_dirs_())
        return false;

    cJSON* seg = cJSON_CreateObject();
    if (!seg)
        return false;
    cJSON_AddNumberToObject(seg, "g_seg", g_seg);
    cJSON_AddNumberToObject(seg, "from_ms", static_cast<double>(from_ms));
    cJSON_AddNumberToObject(seg, "to_ms", static_cast<double>(to_ms));
    cJSON_AddNumberToObject(seg, "verified_at",
                            static_cast<double>(static_cast<int64_t>(std::time(nullptr))));
    cJSON* hashes = cJSON_AddArrayToObject(seg, "hashes");

    int written = 0;
    for (const CachedBlock& cb : blocks)
    {
        if (cb.block.hash.empty())
            continue;
        if (cb.block.timestamp < from_ms || cb.block.timestamp >= to_ms)
            continue;
        const std::string json = block_to_json_(cb);
        if (json.empty())
            continue;
        if (!write_gzip_file_(block_path_(cb.block.hash), json))
            continue;
        cJSON_AddItemToArray(hashes, cJSON_CreateString(cb.block.hash.c_str()));
        ++written;
    }
    if (written == 0)
    {
        cJSON_Delete(seg);
        return false;
    }

    char* printed = cJSON_PrintUnformatted(seg);
    cJSON_Delete(seg);
    if (!printed)
        return false;
    const bool seg_ok = write_text_file_(segment_path_(g_seg), printed);
    cJSON_free(printed);
    if (!seg_ok)
        return false;

    std::vector<SegmentMeta> segs;
    int64_t man_gen = genesis_ms;
    load_manifest_(segs, man_gen);
    if (genesis_ms > 0)
        man_gen = genesis_ms;

    // Upsert this segment.
    bool found = false;
    for (SegmentMeta& m : segs)
    {
        if (m.g_seg == g_seg)
        {
            m.from_ms = from_ms;
            m.to_ms = to_ms;
            m.verified_at = static_cast<int64_t>(std::time(nullptr));
            m.block_count = written;
            found = true;
            break;
        }
    }
    if (!found)
    {
        SegmentMeta m;
        m.g_seg = g_seg;
        m.from_ms = from_ms;
        m.to_ms = to_ms;
        m.verified_at = static_cast<int64_t>(std::time(nullptr));
        m.block_count = written;
        segs.push_back(m);
    }
    enforce_lru_(segs);
    const bool ok = save_manifest_(segs, man_gen);
    if (ok)
        std::printf("[disk-cache] saved segment G=%d blocks=%d domain=%s\n", g_seg, written,
                    domain_key_.c_str());
    return ok;
}

SegmentDiskCache::LoadResult SegmentDiskCache::load_recent(
    int g_live, int max_segments, std::vector<CachedBlock>& out_blocks) const
{
    LoadResult r;
    out_blocks.clear();
    if (!enabled() || max_segments <= 0)
        return r;

    std::vector<SegmentMeta> segs;
    int64_t genesis_ms = 0;
    if (!load_manifest_(segs, genesis_ms))
        return r;
    r.genesis_ms = genesis_ms;

    std::sort(segs.begin(), segs.end(),
              [](const SegmentMeta& a, const SegmentMeta& b) { return a.g_seg > b.g_seg; });

    // Prefer segments at or below g_live (historical + recent).
    std::vector<SegmentMeta> pick;
    for (const SegmentMeta& m : segs)
    {
        if (g_live >= 0 && m.g_seg > g_live)
            continue;
        pick.push_back(m);
        if (static_cast<int>(pick.size()) >= max_segments)
            break;
    }
    // Load oldest first so graph builds tip-ward.
    std::sort(pick.begin(), pick.end(),
              [](const SegmentMeta& a, const SegmentMeta& b) { return a.g_seg < b.g_seg; });

    std::unordered_set<std::string> seen;
    for (const SegmentMeta& m : pick)
    {
        std::string body;
        if (!read_text_file_(segment_path_(m.g_seg), body))
            continue;
        cJSON* root = cJSON_Parse(body.c_str());
        if (!root)
            continue;
        cJSON* hashes = cJSON_GetObjectItem(root, "hashes");
        int got = 0;
        if (cJSON_IsArray(hashes))
        {
            cJSON* h = nullptr;
            cJSON_ArrayForEach(h, hashes)
            {
                if (!cJSON_IsString(h) || !h->valuestring)
                    continue;
                const std::string hash = h->valuestring;
                if (!seen.insert(hash).second)
                    continue;
                std::string bj;
                if (!read_gzip_file_(block_path_(hash), bj))
                    continue;
                CachedBlock cb;
                if (!json_to_block_(bj, cb))
                    continue;
                out_blocks.push_back(std::move(cb));
                ++got;
            }
        }
        cJSON_Delete(root);
        if (got > 0)
        {
            ++r.segments_loaded;
            r.blocks_loaded += got;
            auto keys = chunk_keys_for_window(m.from_ms, m.to_ms);
            r.chunk_keys.insert(r.chunk_keys.end(), keys.begin(), keys.end());
        }
    }
    if (r.segments_loaded > 0)
        std::printf("[disk-cache] bootstrap domain=%s segments=%d blocks=%d\n",
                    domain_key_.c_str(), r.segments_loaded, r.blocks_loaded);
    return r;
}
