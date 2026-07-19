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
    }
}

std::vector<std::string> SegmentDiskCache::hashes_for_segment_(int g_seg) const
{
    std::vector<std::string> out;
    std::string body;
    if (!read_text_file_(segment_path_(g_seg), body))
        return out;
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root)
        return out;
    cJSON* hashes = cJSON_GetObjectItem(root, "hashes");
    if (cJSON_IsArray(hashes))
    {
        cJSON* h = nullptr;
        cJSON_ArrayForEach(h, hashes)
        {
            if (cJSON_IsString(h) && h->valuestring)
                out.push_back(h->valuestring);
        }
    }
    cJSON_Delete(root);
    return out;
}

void SegmentDiskCache::garbage_collect_orphans_(const std::vector<SegmentMeta>& keep) const
{
    std::unordered_set<std::string> live;
    for (const SegmentMeta& m : keep)
    {
        for (const std::string& h : hashes_for_segment_(m.g_seg))
            live.insert(h);
    }
    const fs::path blocks_root = root_dir_() + "\\blocks";
    std::error_code ec;
    if (!fs::exists(blocks_root, ec))
        return;
    int removed = 0;
    for (fs::recursive_directory_iterator it(blocks_root, ec), end; it != end && !ec; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;
        const std::string name = it->path().filename().string();
        // name = <hash>.json.gz
        if (name.size() < 9 || name.find(".json.gz") == std::string::npos)
            continue;
        const std::string hash = name.substr(0, name.size() - 8); // strip .json.gz
        if (live.count(hash) != 0)
            continue;
        fs::remove(it->path(), ec);
        ++removed;
    }
    if (removed > 0)
        std::printf("[disk-cache] orphan GC removed %d block files domain=%s\n", removed,
                    domain_key_.c_str());
}

uint64_t SegmentDiskCache::approx_disk_bytes_() const
{
    uint64_t total = 0;
    std::error_code ec;
    const fs::path root = root_dir_();
    if (!fs::exists(root, ec))
        return 0;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec))
    {
        if (it->is_regular_file(ec))
            total += static_cast<uint64_t>(it->file_size(ec));
    }
    return total;
}

void SegmentDiskCache::enforce_disk_budget_(std::vector<SegmentMeta>& segs,
                                           int64_t genesis_ms) const
{
    enforce_lru_(segs);
    garbage_collect_orphans_(segs);
    while (approx_disk_bytes_() > kMaxDiskBytes && !segs.empty())
    {
        std::sort(segs.begin(), segs.end(),
                  [](const SegmentMeta& a, const SegmentMeta& b) { return a.g_seg < b.g_seg; });
        const SegmentMeta drop = segs.front();
        segs.erase(segs.begin());
        std::error_code ec;
        fs::remove(segment_path_(drop.g_seg), ec);
        garbage_collect_orphans_(segs);
        std::printf("[disk-cache] disk budget: dropped G=%d domain=%s\n", drop.g_seg,
                    domain_key_.c_str());
    }
    save_manifest_(segs, genesis_ms);
}

SegmentDiskCache::CacheStats SegmentDiskCache::stats() const
{
    CacheStats s;
    s.enabled = enabled();
    s.root = root_dir_();
    if (!enabled())
        return s;
    std::vector<SegmentMeta> segs;
    int64_t gen = 0;
    if (load_manifest_(segs, gen))
        s.segments = static_cast<int>(segs.size());
    s.bytes = static_cast<int64_t>(approx_disk_bytes_());
    return s;
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
    int skipped_ts = 0;
    int skipped_io = 0;
    for (const CachedBlock& cb : blocks)
    {
        if (cb.block.hash.empty())
            continue;
        // Inclusive [from_ms, to_ms] so open live tip edge still caches.
        const int64_t ts = cb.block.timestamp;
        if (ts < from_ms || ts > to_ms)
        {
            ++skipped_ts;
            continue;
        }
        const std::string json = block_to_json_(cb);
        if (json.empty())
        {
            ++skipped_io;
            continue;
        }
        if (!write_gzip_file_(block_path_(cb.block.hash), json))
        {
            ++skipped_io;
            std::printf("[disk-cache] gzip write fail hash=%.12s… path=%s\n",
                        cb.block.hash.c_str(), block_path_(cb.block.hash).c_str());
            continue;
        }
        cJSON_AddItemToArray(hashes, cJSON_CreateString(cb.block.hash.c_str()));
        ++written;
    }
    if (written == 0)
    {
        cJSON_Delete(seg);
        std::printf("[disk-cache] save empty G=%d candidates=%zu skip_ts=%d skip_io=%d "
                    "window=[%lld,%lld]\n",
                    g_seg, blocks.size(), skipped_ts, skipped_io,
                    static_cast<long long>(from_ms), static_cast<long long>(to_ms));
        return false;
    }

    char* printed = cJSON_PrintUnformatted(seg);
    cJSON_Delete(seg);
    if (!printed)
        return false;
    const bool seg_ok = write_text_file_(segment_path_(g_seg), printed);
    cJSON_free(printed);
    if (!seg_ok)
    {
        std::printf("[disk-cache] segment meta write fail G=%d path=%s\n", g_seg,
                    segment_path_(g_seg).c_str());
        return false;
    }

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
    enforce_disk_budget_(segs, man_gen);
    std::printf("[disk-cache] saved segment G=%d blocks=%d domain=%s root=%s\n", g_seg, written,
                domain_key_.c_str(), root_dir_().c_str());
    return true;
}

SegmentDiskCache::LoadResult SegmentDiskCache::load_recent(
    int g_live, int max_segments, std::vector<CachedBlock>& out_blocks) const
{
    LoadResult r;
    out_blocks.clear();
    r.root = root_dir_();
    if (!enabled() || max_segments <= 0)
    {
        std::printf("[disk-cache] load skip enabled=%d domain=%s\n", enabled() ? 1 : 0,
                    domain_key_.c_str());
        return r;
    }

    std::vector<SegmentMeta> segs;
    int64_t genesis_ms = 0;
    if (!load_manifest_(segs, genesis_ms))
    {
        std::printf("[disk-cache] no manifest at %s\n", manifest_path_().c_str());
        return r;
    }
    r.manifest_ok = true;
    r.genesis_ms = genesis_ms;

    // Recompute g_live from manifest genesis when possible.
    int effective_live = g_live;
    if (genesis_ms > 0)
    {
        const int64_t now_ms = static_cast<int64_t>(std::time(nullptr)) * 1000;
        const int64_t w = static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
        if (w > 0 && now_ms > genesis_ms)
            effective_live = static_cast<int>((now_ms - genesis_ms) / w);
    }

    std::sort(segs.begin(), segs.end(),
              [](const SegmentMeta& a, const SegmentMeta& b) { return a.g_seg > b.g_seg; });

    // Most recent N by g_seg; only drop absurd future segments (clock skew).
    std::vector<SegmentMeta> pick;
    for (const SegmentMeta& m : segs)
    {
        if (effective_live >= 0 && m.g_seg > effective_live + 1)
            continue;
        pick.push_back(m);
        if (static_cast<int>(pick.size()) >= max_segments)
            break;
    }
    // Load oldest first so graph builds tip-ward.
    std::sort(pick.begin(), pick.end(),
              [](const SegmentMeta& a, const SegmentMeta& b) { return a.g_seg < b.g_seg; });

    std::unordered_set<std::string> seen;
    int parse_fail = 0;
    int missing_file = 0;
    for (const SegmentMeta& m : pick)
    {
        std::string body;
        if (!read_text_file_(segment_path_(m.g_seg), body))
        {
            std::printf("[disk-cache] missing segment file G=%d\n", m.g_seg);
            continue;
        }
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
                {
                    ++missing_file;
                    continue;
                }
                CachedBlock cb;
                if (!json_to_block_(bj, cb))
                {
                    ++parse_fail;
                    continue;
                }
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
    std::printf("[disk-cache] load domain=%s manifest_segs=%zu pick=%zu loaded_seg=%d "
                "blocks=%d miss=%d parse_fail=%d g_live=%d path=%s\n",
                domain_key_.c_str(), segs.size(), pick.size(), r.segments_loaded,
                r.blocks_loaded, missing_file, parse_fail, effective_live, r.root.c_str());
    return r;
}
