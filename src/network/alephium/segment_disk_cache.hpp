#pragma once

// Segment-indexed disk cache: unit of storage is genesis segment G_seg.
// Schema v4: hierarchical multi-entry under G_<id>/ — one gzip file per 64s chunk.
// Complete historical segments replace network interval fills.
// Design: docs/segment-disk-cache.md
//
// Layout under <platform cache root>/<domain>/ (see network/platform/net_platform.hpp):
//   manifest.json              # schema 4 index (RAM-cached after open)
//   cache.log
//   segments/G_<id>/meta.json  # bounds + complete + block_count
//   segments/G_<id>/c_<from>.json.gz  # chunk entry: { blocks: [...] }

#include "domain/alph_block.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class SegmentDiskCache
{
public:
    static constexpr int      kSchemaVersion  = 4; // 64s chunks (v3 was 60s)
    static constexpr int      kMaxSegments    = 48;
    static constexpr int      kStartupLoadMax = ALPH_LOAD_RING_SEGMENTS; // 15
    static constexpr int64_t  kChunkMs        = ALPH_SUBSEGMENT_MS; // 64s
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

    bool has_segment(int g_seg) const; // complete G only
    // True if G has any on-disk data (meta and/or c_*.json.gz), complete or warm.
    bool has_any_data(int g_seg) const;
    bool has_chunk(int g_seg, int64_t chunk_from) const;
    // Chunk from_ms keys present on disk (from c_*.json.gz filenames). Sorted ascending.
    std::vector<int64_t> list_present_chunks(int g_seg) const;
    // Meta bounds if known (manifest or meta.json); false if unknown.
    bool segment_bounds(int g_seg, int64_t& from_ms, int64_t& to_ms) const;

    bool load_segment(int g_seg, std::vector<CachedBlock>& out, int64_t& from_ms,
                      int64_t& to_ms) const;
    // Stream blocks. require_complete=false allows partial/warm G (only present chunks).
    bool load_segment_visit(int g_seg, int64_t& from_ms, int64_t& to_ms,
                            const std::function<void(CachedBlock&&)>& on_block,
                            bool require_complete = true) const;
    // Load only listed chunk keys (must exist). Returns blocks admitted count via visitor.
    bool load_chunks_visit(int g_seg, const std::vector<int64_t>& chunk_from_keys,
                           const std::function<void(CachedBlock&&)>& on_block) const;
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
    std::string segment_dir_(int g_seg) const;          // segments/G_<id>/
    std::string segment_meta_path_(int g_seg) const;    // .../meta.json
    std::string segment_chunk_path_(int g_seg, int64_t chunk_from) const;
    std::string legacy_v2_pack_path_(int g_seg) const;  // segments/G_<id>.json.gz

    bool ensure_dirs_() const;
    void wipe_legacy_v1_layout_() const;
    void migrate_v2_packs_if_needed_() const;
    bool write_text_file_(const std::string& path, const std::string& body) const;
    bool read_text_file_(const std::string& path, std::string& out) const;
    bool write_gzip_file_(const std::string& path, const std::string& body) const;
    bool read_gzip_file_(const std::string& path, std::string& out) const;

    bool ensure_manifest_loaded_() const;
    void invalidate_manifest_cache_() const;
    bool load_manifest_from_disk_(std::vector<SegmentMeta>& segs, int64_t& genesis_ms) const;
    bool save_manifest_(const std::vector<SegmentMeta>& segs, int64_t genesis_ms) const;
    void enforce_lru_(std::vector<SegmentMeta>& segs) const;
    void garbage_collect_orphans_(const std::vector<SegmentMeta>& keep) const;
    void enforce_disk_budget_(std::vector<SegmentMeta>& segs, int64_t genesis_ms) const;
    uint64_t approx_disk_bytes_() const;
    bool find_meta_(int g_seg, SegmentMeta& out) const;
    void remove_segment_files_(int g_seg) const;

    static cJSON* block_to_cjson_(const CachedBlock& b);
    static bool   cjson_to_block_(cJSON* o, CachedBlock& out);
    static int64_t chunk_floor_(int64_t ts_ms, int64_t from_ms);

    std::string domain_key_;
    // RAM-resident manifest (mutable for const loaders).
    mutable bool manifest_loaded_ = false;
    mutable int64_t cached_genesis_ms_ = 0;
    mutable std::vector<SegmentMeta> cached_segs_;
};
