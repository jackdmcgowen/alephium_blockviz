#include "network/pch.h"
#include "network/alephium/segment_disk_cache.hpp"
#include "network/network_domain.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <zlib.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
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
} // namespace

SegmentDiskCache::SegmentDiskCache(std::string domain_key)
{
    set_domain(std::move(domain_key));
}

void SegmentDiskCache::log_event(const char* fmt, ...) const
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    std::printf("%s\n", line);
    std::fflush(stdout);

    if (!enabled())
        return;
    ensure_dirs_();
    const std::string path = root_dir_() + "\\cache.log";
    std::ofstream out(path, std::ios::app);
    if (!out)
        return;
    out << static_cast<long long>(std::time(nullptr)) << ' ' << line << '\n';
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
    invalidate_manifest_cache_();
    if (enabled())
    {
        ensure_dirs_();
        wipe_legacy_v1_layout_();
        migrate_v2_packs_if_needed_();
        ensure_manifest_loaded_();
        if (!manifest_loaded_)
            save_manifest_({}, 0);
        log_event("[disk-cache] domain=%s root=%s schema=%d (G_dir+chunks)", domain_key_.c_str(),
                  root_dir_().c_str(), kSchemaVersion);
    }
    else
    {
        std::printf("[disk-cache] domain=%s enabled=0\n", domain_key_.c_str());
        std::fflush(stdout);
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

int64_t SegmentDiskCache::chunk_floor_(int64_t ts_ms, int64_t from_ms)
{
    if (ts_ms < from_ms)
        return from_ms;
    const int64_t rel = ts_ms - from_ms;
    return from_ms + (rel / kChunkMs) * kChunkMs;
}

std::string SegmentDiskCache::root_dir_() const
{
    return local_app_data_() + "\\AlephiumBlockViz\\cache\\" + domain_key_;
}

std::string SegmentDiskCache::manifest_path_() const
{
    return root_dir_() + "\\manifest.json";
}

std::string SegmentDiskCache::segment_dir_(int g_seg) const
{
    return root_dir_() + "\\segments\\G_" + std::to_string(g_seg);
}

std::string SegmentDiskCache::segment_meta_path_(int g_seg) const
{
    return segment_dir_(g_seg) + "\\meta.json";
}

std::string SegmentDiskCache::segment_chunk_path_(int g_seg, int64_t chunk_from) const
{
    return segment_dir_(g_seg) + "\\c_" + std::to_string(chunk_from) + ".json.gz";
}

std::string SegmentDiskCache::legacy_v2_pack_path_(int g_seg) const
{
    return root_dir_() + "\\segments\\G_" + std::to_string(g_seg) + ".json.gz";
}

bool SegmentDiskCache::ensure_dirs_() const
{
    if (!enabled())
        return false;
    std::error_code ec;
    fs::create_directories(root_dir_() + "\\segments", ec);
    if (ec)
    {
        std::printf("[disk-cache] create_directories failed root=%s ec=%s\n",
                    root_dir_().c_str(), ec.message().c_str());
        std::fflush(stdout);
        return false;
    }
    return true;
}

void SegmentDiskCache::wipe_legacy_v1_layout_() const
{
    std::error_code ec;
    const fs::path blocks = root_dir_() + "\\blocks";
    if (fs::exists(blocks, ec))
    {
        fs::remove_all(blocks, ec);
        log_event("[disk-cache] removed legacy v1 blocks/ tree");
    }
}

void SegmentDiskCache::invalidate_manifest_cache_() const
{
    manifest_loaded_ = false;
    cached_segs_.clear();
    cached_genesis_ms_ = 0;
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
    gzFile gz = gzopen(path.c_str(), "wb6");
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
    char buf[16384];
    int n = 0;
    while ((n = gzread(gz, buf, sizeof(buf))) > 0)
        out.append(buf, static_cast<size_t>(n));
    gzclose(gz);
    return !out.empty() || n >= 0;
}

cJSON* SegmentDiskCache::block_to_cjson_(const CachedBlock& b)
{
    cJSON* o = cJSON_CreateObject();
    if (!o)
        return nullptr;
    cJSON_AddStringToObject(o, "hash", b.block.hash.c_str());
    cJSON_AddNumberToObject(o, "chainFrom", b.block.chainFrom);
    cJSON_AddNumberToObject(o, "chainTo", b.block.chainTo);
    cJSON_AddNumberToObject(o, "height", b.block.height);
    cJSON_AddNumberToObject(o, "timestamp", static_cast<double>(b.block.timestamp));
    cJSON_AddNumberToObject(o, "txn_count", b.block.txn_count);
    if (!b.block.alph_out_atto.empty())
        cJSON_AddStringToObject(o, "alph_out_atto", b.block.alph_out_atto.c_str());
    cJSON_AddBoolToObject(o, "confirmed", b.confirmed ? 1 : 0);
    cJSON* deps = cJSON_AddArrayToObject(o, "deps");
    for (const std::string& d : b.block.deps)
        cJSON_AddItemToArray(deps, cJSON_CreateString(d.c_str()));
    cJSON* uncles = cJSON_AddArrayToObject(o, "ghostUncles");
    for (const std::string& u : b.block.uncles)
    {
        cJSON* uo = cJSON_CreateObject();
        cJSON_AddStringToObject(uo, "blockHash", u.c_str());
        cJSON_AddItemToArray(uncles, uo);
    }
    cJSON_AddArrayToObject(o, "transactions");
    return o;
}

bool SegmentDiskCache::cjson_to_block_(cJSON* o, CachedBlock& out)
{
    if (!o)
        return false;
    AlphBlock b(o);
    if (b.hash.empty())
    {
        cJSON* h = cJSON_GetObjectItem(o, "hash");
        if (!h || !cJSON_IsString(h) || !h->valuestring)
            return false;
        b.hash = h->valuestring;
        if (cJSON* x = cJSON_GetObjectItem(o, "chainFrom"))
            b.chainFrom = static_cast<uint8_t>(x->valueint);
        if (cJSON* x = cJSON_GetObjectItem(o, "chainTo"))
            b.chainTo = static_cast<uint8_t>(x->valueint);
        if (cJSON* x = cJSON_GetObjectItem(o, "height"))
            b.height = x->valueint;
        if (cJSON* x = cJSON_GetObjectItem(o, "timestamp"))
            b.timestamp = static_cast<int64_t>(x->valuedouble);
        if (cJSON* deps = cJSON_GetObjectItem(o, "deps"); deps && cJSON_IsArray(deps))
        {
            cJSON* d = nullptr;
            cJSON_ArrayForEach(d, deps)
            {
                if (cJSON_IsString(d) && d->valuestring)
                    b.deps.push_back(d->valuestring);
            }
        }
    }
    if (cJSON* tc = cJSON_GetObjectItem(o, "txn_count"))
        b.txn_count = tc->valueint;
    if (cJSON* ao = cJSON_GetObjectItem(o, "alph_out_atto"); ao && cJSON_IsString(ao))
        b.alph_out_atto = ao->valuestring ? ao->valuestring : "";
    out.block = std::move(b);
    out.confirmed = cJSON_IsTrue(cJSON_GetObjectItem(o, "confirmed"));
    return !out.block.hash.empty();
}

bool SegmentDiskCache::load_manifest_from_disk_(std::vector<SegmentMeta>& segs,
                                               int64_t& genesis_ms) const
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
    if (!ver || (ver->valueint != kSchemaVersion && ver->valueint != 2))
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
            if (cJSON* x = cJSON_GetObjectItem(it, "complete"))
                m.complete = cJSON_IsTrue(x) || (cJSON_IsNumber(x) && x->valueint != 0);
            if (m.g_seg >= 0 && m.to_ms > m.from_ms)
                segs.push_back(m);
        }
    }
    cJSON_Delete(root);
    return true;
}

bool SegmentDiskCache::ensure_manifest_loaded_() const
{
    if (manifest_loaded_)
        return true;
    if (!enabled())
        return false;
    int64_t gen = 0;
    std::vector<SegmentMeta> segs;
    if (!load_manifest_from_disk_(segs, gen))
        return false;
    cached_segs_ = std::move(segs);
    cached_genesis_ms_ = gen;
    manifest_loaded_ = true;
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
        cJSON_AddBoolToObject(o, "complete", m.complete ? 1 : 0);
        cJSON_AddItemToArray(arr, o);
    }
    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed)
        return false;
    const bool ok = write_text_file_(manifest_path_(), printed);
    cJSON_free(printed);
    if (ok)
    {
        cached_segs_ = segs;
        cached_genesis_ms_ = genesis_ms;
        manifest_loaded_ = true;
    }
    return ok;
}

void SegmentDiskCache::remove_segment_files_(int g_seg) const
{
    std::error_code ec;
    fs::remove_all(segment_dir_(g_seg), ec);
    fs::remove(legacy_v2_pack_path_(g_seg), ec);
}

void SegmentDiskCache::enforce_lru_(std::vector<SegmentMeta>& segs) const
{
    if (static_cast<int>(segs.size()) <= kMaxSegments)
        return;
    std::sort(segs.begin(), segs.end(),
              [](const SegmentMeta& a, const SegmentMeta& b) { return a.g_seg > b.g_seg; });
    while (static_cast<int>(segs.size()) > kMaxSegments)
    {
        const SegmentMeta drop = segs.back();
        segs.pop_back();
        remove_segment_files_(drop.g_seg);
    }
}

void SegmentDiskCache::garbage_collect_orphans_(const std::vector<SegmentMeta>& keep) const
{
    std::unordered_set<int> live;
    for (const SegmentMeta& m : keep)
        live.insert(m.g_seg);

    const fs::path segs = root_dir_() + "\\segments";
    std::error_code ec;
    if (!fs::exists(segs, ec))
        return;
    int removed = 0;
    for (fs::directory_iterator it(segs, ec), end; it != end && !ec; it.increment(ec))
    {
        const std::string name = it->path().filename().string();
        if (name.rfind("G_", 0) != 0)
            continue;
        int g = -1;
        try
        {
            // G_123 or G_123.json.gz
            const auto rest = name.substr(2);
            const auto dot = rest.find('.');
            g = std::stoi(dot == std::string::npos ? rest : rest.substr(0, dot));
        }
        catch (...)
        {
            continue;
        }
        if (live.count(g) != 0)
            continue;
        if (it->is_directory(ec))
            fs::remove_all(it->path(), ec);
        else
            fs::remove(it->path(), ec);
        ++removed;
    }
    if (removed > 0)
        log_event("[disk-cache] GC removed %d orphan segment trees", removed);
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
        remove_segment_files_(drop.g_seg);
        garbage_collect_orphans_(segs);
        log_event("[disk-cache] disk budget dropped G=%d", drop.g_seg);
    }
    save_manifest_(segs, genesis_ms);
}

void SegmentDiskCache::migrate_v2_packs_if_needed_() const
{
    if (!enabled())
        return;
    const fs::path segs = root_dir_() + "\\segments";
    std::error_code ec;
    if (!fs::exists(segs, ec))
        return;

    int migrated = 0;
    for (fs::directory_iterator it(segs, ec), end; it != end && !ec; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;
        const std::string name = it->path().filename().string();
        // G_<id>.json.gz (v2 flat pack)
        if (name.size() < 10 || name.rfind("G_", 0) != 0 ||
            name.find(".json.gz") == std::string::npos)
            continue;
        if (name.find("\\") != std::string::npos)
            continue;
        int g = -1;
        try
        {
            const auto dot = name.find('.');
            g = std::stoi(name.substr(2, dot - 2));
        }
        catch (...)
        {
            continue;
        }
        // Skip if already v3 dir
        if (fs::exists(segment_dir_(g), ec))
        {
            fs::remove(it->path(), ec);
            continue;
        }

        std::string body;
        if (!read_gzip_file_(it->path().string(), body))
            continue;
        cJSON* root = cJSON_Parse(body.c_str());
        if (!root)
            continue;
        int64_t from_ms = 0, to_ms = 0;
        if (cJSON* x = cJSON_GetObjectItem(root, "from_ms"))
            from_ms = static_cast<int64_t>(x->valuedouble);
        if (cJSON* x = cJSON_GetObjectItem(root, "to_ms"))
            to_ms = static_cast<int64_t>(x->valuedouble);
        bool complete = cJSON_IsTrue(cJSON_GetObjectItem(root, "complete"));
        std::vector<CachedBlock> blocks;
        cJSON* arr = cJSON_GetObjectItem(root, "blocks");
        if (cJSON_IsArray(arr))
        {
            cJSON* bo = nullptr;
            cJSON_ArrayForEach(bo, arr)
            {
                CachedBlock cb;
                if (cjson_to_block_(bo, cb))
                    blocks.push_back(std::move(cb));
            }
        }
        cJSON_Delete(root);
        if (blocks.empty() || to_ms <= from_ms)
        {
            fs::remove(it->path(), ec);
            continue;
        }
        // Re-save via public path (v3 multi-chunk).
        const_cast<SegmentDiskCache*>(this)->save_segment(g, from_ms, to_ms, cached_genesis_ms_,
                                                          blocks, complete);
        fs::remove(it->path(), ec);
        ++migrated;
    }
    if (migrated > 0)
        log_event("[disk-cache] migrated %d v2 packs to schema %d hierarchy", migrated,
                  kSchemaVersion);
}

SegmentDiskCache::CacheStats SegmentDiskCache::stats() const
{
    CacheStats s;
    s.enabled = enabled();
    s.root = root_dir_();
    if (!enabled())
        return s;
    if (ensure_manifest_loaded_())
    {
        s.segments = static_cast<int>(cached_segs_.size());
        for (const auto& m : cached_segs_)
            if (m.complete)
                ++s.complete_segments;
    }
    s.bytes = static_cast<int64_t>(approx_disk_bytes_());
    return s;
}

bool SegmentDiskCache::find_meta_(int g_seg, SegmentMeta& out) const
{
    if (!ensure_manifest_loaded_())
        return false;
    for (const SegmentMeta& m : cached_segs_)
    {
        if (m.g_seg == g_seg)
        {
            out = m;
            return true;
        }
    }
    return false;
}

bool SegmentDiskCache::has_segment(int g_seg) const
{
    if (!enabled() || g_seg < 0)
        return false;
    SegmentMeta m;
    if (!find_meta_(g_seg, m))
        return false;
    return m.complete;
}

bool SegmentDiskCache::has_any_data(int g_seg) const
{
    if (!enabled() || g_seg < 0)
        return false;
    SegmentMeta m;
    if (find_meta_(g_seg, m))
        return true;
    std::error_code ec;
    if (fs::exists(segment_dir_(g_seg), ec))
        return true;
    if (fs::exists(legacy_v2_pack_path_(g_seg), ec))
        return true;
    return !list_present_chunks(g_seg).empty();
}

bool SegmentDiskCache::has_chunk(int g_seg, int64_t chunk_from) const
{
    if (!enabled() || g_seg < 0 || chunk_from < 0)
        return false;
    std::error_code ec;
    if (fs::exists(segment_chunk_path_(g_seg, chunk_from), ec))
        return true;
    // Legacy v2 pack is treated as covering all keys only when complete meta says so.
    return false;
}

std::vector<int64_t> SegmentDiskCache::list_present_chunks(int g_seg) const
{
    std::vector<int64_t> keys;
    if (!enabled() || g_seg < 0)
        return keys;
    const fs::path dir = segment_dir_(g_seg);
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
        return keys;
    for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;
        const std::string name = it->path().filename().string();
        // c_<from_ms>.json.gz
        if (name.rfind("c_", 0) != 0 || name.find(".json.gz") == std::string::npos)
            continue;
        try
        {
            const auto und = name.find('_');
            const auto dot = name.find('.');
            if (und == std::string::npos || dot == std::string::npos || dot <= und + 1)
                continue;
            keys.push_back(std::stoll(name.substr(und + 1, dot - und - 1)));
        }
        catch (...)
        {
            continue;
        }
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

bool SegmentDiskCache::segment_bounds(int g_seg, int64_t& from_ms, int64_t& to_ms) const
{
    from_ms = to_ms = 0;
    if (!enabled() || g_seg < 0)
        return false;
    SegmentMeta m;
    if (find_meta_(g_seg, m) && m.to_ms > m.from_ms)
    {
        from_ms = m.from_ms;
        to_ms = m.to_ms;
        return true;
    }
    std::string meta_body;
    if (read_text_file_(segment_meta_path_(g_seg), meta_body))
    {
        cJSON* mroot = cJSON_Parse(meta_body.c_str());
        if (mroot)
        {
            if (cJSON* x = cJSON_GetObjectItem(mroot, "from_ms"))
                from_ms = static_cast<int64_t>(x->valuedouble);
            if (cJSON* x = cJSON_GetObjectItem(mroot, "to_ms"))
                to_ms = static_cast<int64_t>(x->valuedouble);
            cJSON_Delete(mroot);
            if (to_ms > from_ms)
                return true;
        }
    }
    return false;
}

std::vector<int> SegmentDiskCache::list_segment_ids(bool complete_only) const
{
    std::vector<int> ids;
    if (!enabled() || !ensure_manifest_loaded_())
        return ids;
    std::vector<SegmentMeta> segs = cached_segs_;
    std::sort(segs.begin(), segs.end(),
              [](const SegmentMeta& a, const SegmentMeta& b) { return a.g_seg > b.g_seg; });
    for (const SegmentMeta& m : segs)
    {
        if (complete_only && !m.complete)
            continue;
        ids.push_back(m.g_seg);
    }
    return ids;
}

bool SegmentDiskCache::load_chunks_visit(
    int g_seg, const std::vector<int64_t>& chunk_from_keys,
    const std::function<void(CachedBlock&&)>& on_block) const
{
    if (!enabled() || g_seg < 0 || !on_block || chunk_from_keys.empty())
        return false;
    int got = 0;
    for (int64_t key : chunk_from_keys)
    {
        std::string body;
        if (!read_gzip_file_(segment_chunk_path_(g_seg, key), body))
            continue;
        cJSON* root = cJSON_Parse(body.c_str());
        if (!root)
            continue;
        cJSON* blocks = cJSON_GetObjectItem(root, "blocks");
        if (cJSON_IsArray(blocks))
        {
            cJSON* bo = nullptr;
            cJSON_ArrayForEach(bo, blocks)
            {
                CachedBlock cb;
                if (!cjson_to_block_(bo, cb))
                    continue;
                on_block(std::move(cb));
                ++got;
            }
        }
        cJSON_Delete(root);
    }
    return got > 0;
}

bool SegmentDiskCache::load_segment_visit(int g_seg, int64_t& from_ms, int64_t& to_ms,
                                         const std::function<void(CachedBlock&&)>& on_block,
                                         bool require_complete) const
{
    from_ms = to_ms = 0;
    if (!enabled() || g_seg < 0 || !on_block)
        return false;
    SegmentMeta meta;
    const bool have_meta = find_meta_(g_seg, meta);
    if (require_complete)
    {
        if (!have_meta || !meta.complete)
            return false;
    }
    else if (!have_meta && !has_any_data(g_seg))
    {
        return false;
    }

    if (have_meta)
    {
        from_ms = meta.from_ms;
        to_ms = meta.to_ms;
    }
    else
    {
        segment_bounds(g_seg, from_ms, to_ms);
    }

    // Prefer v3 directory of chunk files.
    const fs::path dir = segment_dir_(g_seg);
    std::error_code ec;
    int got = 0;
    if (fs::exists(dir, ec) && fs::is_directory(dir, ec))
    {
        std::string meta_body;
        if (read_text_file_(segment_meta_path_(g_seg), meta_body))
        {
            cJSON* mroot = cJSON_Parse(meta_body.c_str());
            if (mroot)
            {
                if (cJSON* x = cJSON_GetObjectItem(mroot, "from_ms"))
                    from_ms = static_cast<int64_t>(x->valuedouble);
                if (cJSON* x = cJSON_GetObjectItem(mroot, "to_ms"))
                    to_ms = static_cast<int64_t>(x->valuedouble);
                cJSON_Delete(mroot);
            }
        }
        for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec))
        {
            if (!it->is_regular_file(ec))
                continue;
            const std::string name = it->path().filename().string();
            if (name.rfind("c_", 0) != 0 || name.find(".json.gz") == std::string::npos)
                continue;
            std::string body;
            if (!read_gzip_file_(it->path().string(), body))
                continue;
            cJSON* root = cJSON_Parse(body.c_str());
            if (!root)
                continue;
            cJSON* blocks = cJSON_GetObjectItem(root, "blocks");
            if (cJSON_IsArray(blocks))
            {
                cJSON* bo = nullptr;
                cJSON_ArrayForEach(bo, blocks)
                {
                    CachedBlock cb;
                    if (!cjson_to_block_(bo, cb))
                        continue;
                    on_block(std::move(cb));
                    ++got;
                }
            }
            cJSON_Delete(root);
        }
    }
    else if (require_complete || fs::exists(legacy_v2_pack_path_(g_seg), ec))
    {
        // Fallback: legacy v2 single pack (treated as full segment content).
        std::string body;
        if (!read_gzip_file_(legacy_v2_pack_path_(g_seg), body))
            return false;
        cJSON* root = cJSON_Parse(body.c_str());
        if (!root)
            return false;
        if (cJSON* x = cJSON_GetObjectItem(root, "from_ms"))
            from_ms = static_cast<int64_t>(x->valuedouble);
        if (cJSON* x = cJSON_GetObjectItem(root, "to_ms"))
            to_ms = static_cast<int64_t>(x->valuedouble);
        cJSON* blocks = cJSON_GetObjectItem(root, "blocks");
        if (cJSON_IsArray(blocks))
        {
            cJSON* bo = nullptr;
            cJSON_ArrayForEach(bo, blocks)
            {
                CachedBlock cb;
                if (!cjson_to_block_(bo, cb))
                    continue;
                on_block(std::move(cb));
                ++got;
            }
        }
        cJSON_Delete(root);
    }

    if (got <= 0)
    {
        log_event("[disk-cache] load_segment G=%d empty complete_req=%d", g_seg,
                  require_complete ? 1 : 0);
        return false;
    }
    log_event("[disk-cache] load_segment G=%d blocks=%d multi-chunk complete_req=%d", g_seg, got,
              require_complete ? 1 : 0);
    return true;
}

bool SegmentDiskCache::load_segment(int g_seg, std::vector<CachedBlock>& out, int64_t& from_ms,
                                    int64_t& to_ms) const
{
    out.clear();
    return load_segment_visit(
        g_seg, from_ms, to_ms,
        [&](CachedBlock&& cb) { out.push_back(std::move(cb)); },
        /*require_complete=*/true);
}

bool SegmentDiskCache::save_segment(int g_seg, int64_t from_ms, int64_t to_ms,
                                    int64_t genesis_ms,
                                    const std::vector<CachedBlock>& blocks, bool complete)
{
    if (!enabled() || g_seg < 0 || to_ms <= from_ms || blocks.empty())
        return false;
    if (!ensure_dirs_())
    {
        log_event("[disk-cache] ensure_dirs failed root=%s", root_dir_().c_str());
        return false;
    }

    // Bucket blocks into 60s chunk files (multi-entry hierarchy under G_dir).
    std::unordered_map<int64_t, std::vector<const CachedBlock*>> by_chunk;
    int written = 0;
    for (const CachedBlock& cb : blocks)
    {
        if (cb.block.hash.empty())
            continue;
        const int64_t ts = cb.block.timestamp;
        if (ts < from_ms || ts > to_ms)
            continue;
        const int64_t key = chunk_floor_(ts, from_ms);
        by_chunk[key].push_back(&cb);
        ++written;
    }
    if (written == 0)
    {
        log_event("[disk-cache] save empty G=%d candidates=%zu window=[%lld,%lld]",
                  g_seg, blocks.size(), static_cast<long long>(from_ms),
                  static_cast<long long>(to_ms));
        return false;
    }

    std::error_code ec;
    fs::create_directories(segment_dir_(g_seg), ec);
    // Remove old chunk files for this G (rewrite set).
    for (fs::directory_iterator it(segment_dir_(g_seg), ec), end; it != end && !ec;
         it.increment(ec))
    {
        if (!it->is_regular_file(ec))
            continue;
        const std::string name = it->path().filename().string();
        if (name.rfind("c_", 0) == 0)
            fs::remove(it->path(), ec);
    }
    // Drop legacy v2 pack if present.
    fs::remove(legacy_v2_pack_path_(g_seg), ec);

    for (const auto& kv : by_chunk)
    {
        cJSON* root = cJSON_CreateObject();
        if (!root)
            continue;
        cJSON_AddNumberToObject(root, "g_seg", g_seg);
        cJSON_AddNumberToObject(root, "chunk_from", static_cast<double>(kv.first));
        cJSON* arr = cJSON_AddArrayToObject(root, "blocks");
        for (const CachedBlock* p : kv.second)
        {
            cJSON* bo = block_to_cjson_(*p);
            if (bo)
                cJSON_AddItemToArray(arr, bo);
        }
        char* printed = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!printed)
            continue;
        write_gzip_file_(segment_chunk_path_(g_seg, kv.first), printed);
        cJSON_free(printed);
    }

    // meta.json for the G directory
    {
        cJSON* meta = cJSON_CreateObject();
        cJSON_AddNumberToObject(meta, "g_seg", g_seg);
        cJSON_AddNumberToObject(meta, "from_ms", static_cast<double>(from_ms));
        cJSON_AddNumberToObject(meta, "to_ms", static_cast<double>(to_ms));
        cJSON_AddBoolToObject(meta, "complete", complete ? 1 : 0);
        cJSON_AddNumberToObject(meta, "block_count", written);
        cJSON_AddNumberToObject(meta, "chunk_count", static_cast<double>(by_chunk.size()));
        cJSON_AddNumberToObject(meta, "verified_at",
                                static_cast<double>(static_cast<int64_t>(std::time(nullptr))));
        char* printed = cJSON_PrintUnformatted(meta);
        cJSON_Delete(meta);
        if (printed)
        {
            write_text_file_(segment_meta_path_(g_seg), printed);
            cJSON_free(printed);
        }
    }

    std::vector<SegmentMeta> segs;
    int64_t man_gen = genesis_ms;
    if (ensure_manifest_loaded_())
    {
        segs = cached_segs_;
        if (cached_genesis_ms_ > 0)
            man_gen = cached_genesis_ms_;
    }
    if (genesis_ms > 0)
        man_gen = genesis_ms;

    bool found = false;
    for (SegmentMeta& m : segs)
    {
        if (m.g_seg == g_seg)
        {
            m.from_ms = from_ms;
            m.to_ms = to_ms;
            m.verified_at = static_cast<int64_t>(std::time(nullptr));
            m.block_count = written;
            m.complete = complete;
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
        m.complete = complete;
        segs.push_back(m);
    }
    enforce_disk_budget_(segs, man_gen);
    log_event("[disk-cache] saved G=%d blocks=%d chunks=%zu complete=%d (schema %d)", g_seg,
              written, by_chunk.size(), complete ? 1 : 0, kSchemaVersion);
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
        log_event("[disk-cache] load skip enabled=%d domain=%s", enabled() ? 1 : 0,
                  domain_key_.c_str());
        return r;
    }

    if (!ensure_manifest_loaded_())
    {
        log_event("[disk-cache] no manifest at %s", manifest_path_().c_str());
        return r;
    }
    r.manifest_ok = true;
    r.genesis_ms = cached_genesis_ms_;

    int effective_live = g_live;
    if (cached_genesis_ms_ > 0)
    {
        const int64_t now_ms = static_cast<int64_t>(std::time(nullptr)) * 1000;
        const int64_t w = static_cast<int64_t>(ALPH_LOOKBACK_WINDOW_SECONDS) * 1000;
        if (w > 0 && now_ms > cached_genesis_ms_)
            effective_live = static_cast<int>((now_ms - cached_genesis_ms_) / w);
    }

    std::vector<int> ids;
    for (const SegmentMeta& m : cached_segs_)
    {
        if (!m.complete)
            continue;
        if (effective_live >= 0 && m.g_seg > effective_live + 1)
            continue;
        ids.push_back(m.g_seg);
    }
    std::sort(ids.begin(), ids.end(), [](int a, int b) { return a > b; });
    if (static_cast<int>(ids.size()) > max_segments)
        ids.resize(static_cast<size_t>(max_segments));
    std::sort(ids.begin(), ids.end());

    std::unordered_set<std::string> seen;
    for (int G : ids)
    {
        int64_t from_ms = 0, to_ms = 0;
        int part_n = 0;
        if (!load_segment_visit(G, from_ms, to_ms, [&](CachedBlock&& cb) {
                if (cb.block.hash.empty() || !seen.insert(cb.block.hash).second)
                    return;
                out_blocks.push_back(std::move(cb));
                ++r.blocks_loaded;
                ++part_n;
            }))
            continue;
        if (part_n <= 0)
            continue;
        ++r.segments_loaded;
        r.segment_ids.push_back(G);
        auto keys = chunk_keys_for_window(from_ms, to_ms);
        r.chunk_keys.insert(r.chunk_keys.end(), keys.begin(), keys.end());
    }
    log_event("[disk-cache] load_recent domain=%s loaded_seg=%d blocks=%d g_live=%d schema=%d",
              domain_key_.c_str(), r.segments_loaded, r.blocks_loaded, effective_live,
              kSchemaVersion);
    return r;
}
