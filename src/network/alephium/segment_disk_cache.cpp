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
    // Best-effort file log next to cache (survives GUI with no console).
    ensure_dirs_();
    const std::string path = root_dir_() + "\\cache.log";
    std::ofstream out(path, std::ios::app);
    if (!out)
        return;
    const auto t = static_cast<long long>(std::time(nullptr));
    out << t << ' ' << line << '\n';
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
    if (enabled())
    {
        // Create folder immediately so user can find the path.
        ensure_dirs_();
        std::vector<SegmentMeta> segs;
        int64_t gen = 0;
        if (!load_manifest_(segs, gen))
            save_manifest_(segs, 0);
        log_event("[disk-cache] domain=%s root=%s enabled=1", domain_key_.c_str(),
                  root_dir_().c_str());
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
    std::error_code ec1, ec2;
    fs::create_directories(root_dir_() + "\\segments", ec1);
    fs::create_directories(root_dir_() + "\\blocks", ec2);
    if (ec1 || ec2)
    {
        std::printf("[disk-cache] create_directories failed root=%s ec1=%s ec2=%s\n",
                    root_dir_().c_str(), ec1.message().c_str(), ec2.message().c_str());
        std::fflush(stdout);
        return false;
    }
    return true;
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
            if (cJSON* x = cJSON_GetObjectItem(it, "complete"))
                m.complete = cJSON_IsTrue(x) || (cJSON_IsNumber(x) && x->valueint != 0);
            else
                m.complete = true; // legacy entries treated as complete
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
        cJSON_AddBoolToObject(o, "complete", m.complete ? 1 : 0);
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
    {
        s.segments = static_cast<int>(segs.size());
        for (const auto& m : segs)
            if (m.complete)
                ++s.complete_segments;
    }
    s.bytes = static_cast<int64_t>(approx_disk_bytes_());
    return s;
}

bool SegmentDiskCache::find_meta_(int g_seg, SegmentMeta& out) const
{
    std::vector<SegmentMeta> segs;
    int64_t gen = 0;
    if (!load_manifest_(segs, gen))
        return false;
    for (const SegmentMeta& m : segs)
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

std::vector<int> SegmentDiskCache::list_segment_ids(bool complete_only) const
{
    std::vector<int> ids;
    if (!enabled())
        return ids;
    std::vector<SegmentMeta> segs;
    int64_t gen = 0;
    if (!load_manifest_(segs, gen))
        return ids;
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

bool SegmentDiskCache::load_segment(int g_seg, std::vector<CachedBlock>& out, int64_t& from_ms,
                                    int64_t& to_ms) const
{
    out.clear();
    from_ms = to_ms = 0;
    if (!enabled() || g_seg < 0)
        return false;
    SegmentMeta meta;
    if (!find_meta_(g_seg, meta) || !meta.complete)
        return false;
    from_ms = meta.from_ms;
    to_ms = meta.to_ms;

    std::string body;
    if (!read_text_file_(segment_path_(g_seg), body))
    {
        std::printf("[disk-cache] load_segment G=%d missing meta file\n", g_seg);
        return false;
    }
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root)
        return false;
    cJSON* hashes = cJSON_GetObjectItem(root, "hashes");
    int got = 0;
    if (cJSON_IsArray(hashes))
    {
        cJSON* h = nullptr;
        cJSON_ArrayForEach(h, hashes)
        {
            if (!cJSON_IsString(h) || !h->valuestring)
                continue;
            std::string bj;
            if (!read_gzip_file_(block_path_(h->valuestring), bj))
                continue;
            CachedBlock cb;
            if (!json_to_block_(bj, cb))
                continue;
            out.push_back(std::move(cb));
            ++got;
        }
    }
    cJSON_Delete(root);
    if (got <= 0)
    {
        std::printf("[disk-cache] load_segment G=%d no blocks readable\n", g_seg);
        return false;
    }
    log_event("[disk-cache] load_segment G=%d blocks=%d complete=1", g_seg, got);
    return true;
}

bool SegmentDiskCache::save_segment(int g_seg, int64_t from_ms, int64_t to_ms,
                                    int64_t genesis_ms,
                                    const std::vector<CachedBlock>& blocks, bool complete)
{
    if (!enabled() || g_seg < 0 || to_ms <= from_ms || blocks.empty())
        return false;
    if (!ensure_dirs_())
    {
        std::printf("[disk-cache] ensure_dirs failed root=%s\n", root_dir_().c_str());
        return false;
    }

    cJSON* seg = cJSON_CreateObject();
    if (!seg)
        return false;
    cJSON_AddNumberToObject(seg, "g_seg", g_seg);
    cJSON_AddNumberToObject(seg, "from_ms", static_cast<double>(from_ms));
    cJSON_AddNumberToObject(seg, "to_ms", static_cast<double>(to_ms));
    cJSON_AddBoolToObject(seg, "complete", complete ? 1 : 0);
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

    // Upsert this segment (index by G only).
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
    log_event("[disk-cache] saved G=%d blocks=%d complete=%d domain=%s root=%s", g_seg, written,
              complete ? 1 : 0, domain_key_.c_str(), root_dir_().c_str());
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

    // Complete segments only, newest G first, then load whole segment units oldest-first.
    std::vector<int> ids;
    for (const SegmentMeta& m : segs)
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
    std::sort(ids.begin(), ids.end()); // oldest first admit

    std::unordered_set<std::string> seen;
    for (int G : ids)
    {
        std::vector<CachedBlock> part;
        int64_t from_ms = 0, to_ms = 0;
        if (!load_segment(G, part, from_ms, to_ms))
            continue;
        for (auto& cb : part)
        {
            if (cb.block.hash.empty() || !seen.insert(cb.block.hash).second)
                continue;
            out_blocks.push_back(std::move(cb));
            ++r.blocks_loaded;
        }
        ++r.segments_loaded;
        r.segment_ids.push_back(G);
        auto keys = chunk_keys_for_window(from_ms, to_ms);
        r.chunk_keys.insert(r.chunk_keys.end(), keys.begin(), keys.end());
    }
    std::printf("[disk-cache] load_recent domain=%s complete_ids=%zu loaded_seg=%d blocks=%d "
                "g_live=%d path=%s\n",
                domain_key_.c_str(), ids.size(), r.segments_loaded, r.blocks_loaded,
                effective_live, r.root.c_str());
    return r;
}
