#pragma once

// Segment-indexed disk cache: unit of storage/load is genesis segment G_seg.
// One gzip pack per G (meta + all blocks). Complete segments replace network fills.
// Design: docs/segment-disk-cache.md
//
// Layout under %LOCALAPPDATA%/AlephiumBlockViz/cache/<domain>/:
//   manifest.json
//   cache.log
//   segments/G_<id>.json.gz   // single compressed pack for that G

#include "domain/alph_block.hpp"

#include <cstdint>
#include <string>
#include <vector>

class SegmentDiskCache
{
public:
    static constexpr int      kSchemaVersion  = 2; // pack-per-segment (no per-block files)
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
        bool    complete = false;
    };

    struct LoadResult
    {
        int         segments_loaded = 0;
        int         blocks_loaded = 0;
        int64_t     genesis_ms = 0;
        bool        manifest_ok = false;
        std::string root;
        std::vector<int64_t> chunk_keys;
        std::vector<int>     segment_ids;
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
    bool load_segment(int g_seg, std::vector<CachedBlock>& out, int64_t& from_ms,
                      int64_t& to_ms) const;
    std::vector<int> list_segment_ids(bool complete_only = true) const;

    bool save_segment(int g_seg, int64_t from_ms, int64_t to_ms, int64_t genesis_ms,
                      const std::vector<CachedBlock>& blocks, bool complete);

    LoadResult load_recent(int g_live, int max_segments,
                           std::vector<CachedBlock>& out_blocks) const;

    CacheStats stats() const;
    void log_event(const char* fmt, ...) const;

    static std::vector<int64_t> chunk_keys_for_window(int64_t from_ms, int64_t to_ms);
    static std::string domain_key_from_int(int domain);

private:
    std::string root_dir_() const;
    std::string manifest_path_() const;
    std::string segment_pack_path_(int g_seg) const; // segments/G_<id>.json.gz

    bool ensure_dirs_() const;
    void wipe_legacy_v1_layout_() const;
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
    bool find_meta_(int g_seg, SegmentMeta& out) const;

    static cJSON* block_to_cjson_(const CachedBlock& b);
    static bool   cjson_to_block_(cJSON* o, CachedBlock& out);

    std::string domain_key_;
};
