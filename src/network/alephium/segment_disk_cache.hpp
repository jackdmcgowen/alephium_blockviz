#pragma once

// Segment-indexed disk cache: unit of storage/load is genesis segment G_seg.
// Complete segments replace network interval loads for that G.
// Design: docs/segment-disk-cache.md
//
// Layout under %LOCALAPPDATA%/AlephiumBlockViz/cache/<domain>/:
//   manifest.json, segments/G_<id>.json, blocks/<hh>/<hash>.json.gz

#include "domain/alph_block.hpp"

#include <cstdint>
#include <string>
#include <vector>

class SegmentDiskCache
{
public:
    static constexpr int      kSchemaVersion  = 1;
    static constexpr int      kMaxSegments    = 48;
    static constexpr int      kStartupLoadMax = 12;
    static constexpr int64_t  kChunkMs        = 60 * 1000;
    static constexpr uint64_t kMaxDiskBytes   = 512ull * 1024 * 1024;

    struct CachedBlock
    {
        AlphBlock block;
        bool      confirmed = false;
    };

    struct SegmentMeta
    {
        int     g_seg = -1;
        int64_t from_ms = 0;
        int64_t to_ms = 0;
        int64_t verified_at = 0;
        int     block_count = 0;
        bool    complete = false; // all interval chunks present at save time
    };

    struct LoadResult
    {
        int         segments_loaded = 0;
        int         blocks_loaded = 0;
        int64_t     genesis_ms = 0;
        bool        manifest_ok = false;
        std::string root;
        std::vector<int64_t> chunk_keys;
        std::vector<int>     segment_ids; // G_seg loaded this call
    };

    struct CacheStats
    {
        bool        enabled = false;
        int         segments = 0;
        int         complete_segments = 0;
        int64_t     bytes = 0;
        std::string root;
    };

    SegmentDiskCache() = default;
    explicit SegmentDiskCache(std::string domain_key);

    void set_domain(std::string domain_key);
    const std::string& domain() const { return domain_key_; }
    bool enabled() const { return !domain_key_.empty() && domain_key_ != "debug"; }

    std::string root_dir() const { return root_dir_(); }

    bool has_segment(int g_seg) const;
    // Only complete segments are loadable for network replace.
    bool load_segment(int g_seg, std::vector<CachedBlock>& out, int64_t& from_ms,
                      int64_t& to_ms) const;
    // Newest-first G ids (complete only if complete_only).
    std::vector<int> list_segment_ids(bool complete_only = true) const;

    // Persist whole segment by G. complete=true → safe to skip network for G.
    bool save_segment(int g_seg, int64_t from_ms, int64_t to_ms, int64_t genesis_ms,
                      const std::vector<CachedBlock>& blocks, bool complete);

    // Bootstrap helper: load up to max complete segments (newest G first).
    LoadResult load_recent(int g_live, int max_segments,
                           std::vector<CachedBlock>& out_blocks) const;

    CacheStats stats() const;

    static std::vector<int64_t> chunk_keys_for_window(int64_t from_ms, int64_t to_ms);
    static std::string domain_key_from_int(int domain);

private:
    std::string root_dir_() const;
    std::string manifest_path_() const;
    std::string segment_path_(int g_seg) const;
    std::string block_path_(const std::string& hash) const;

    bool ensure_dirs_() const;
    bool write_text_file_(const std::string& path, const std::string& body) const;
    bool read_text_file_(const std::string& path, std::string& out) const;
    bool write_gzip_file_(const std::string& path, const std::string& body) const;
    bool read_gzip_file_(const std::string& path, std::string& out) const;

    bool load_manifest_(std::vector<SegmentMeta>& segs, int64_t& genesis_ms) const;
    bool save_manifest_(const std::vector<SegmentMeta>& segs, int64_t genesis_ms) const;
    void enforce_lru_(std::vector<SegmentMeta>& segs) const;
    void garbage_collect_orphans_(const std::vector<SegmentMeta>& keep) const;
    void enforce_disk_budget_(std::vector<SegmentMeta>& segs, int64_t genesis_ms) const;
    uint64_t approx_disk_bytes_() const;
    std::vector<std::string> hashes_for_segment_(int g_seg) const;
    bool find_meta_(int g_seg, SegmentMeta& out) const;

    static std::string block_to_json_(const CachedBlock& b);
    static bool        json_to_block_(const std::string& json, CachedBlock& out);

    std::string domain_key_;
};
