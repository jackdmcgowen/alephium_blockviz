#pragma once

// Verified timeline-segment disk cache (bootstrap before network fill).
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
    static constexpr int kSchemaVersion   = 1;
    static constexpr int kMaxSegments     = 48;  // LRU retain
    static constexpr int kStartupLoadMax  = 12;  // bootstrap paint budget
    static constexpr int64_t kChunkMs     = 60 * 1000;

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
    };

    struct LoadResult
    {
        int segments_loaded = 0;
        int blocks_loaded = 0;
        int64_t genesis_ms = 0;
        // Chunk from_ms keys matching adapter history_slots_fetched_ convention.
        std::vector<int64_t> chunk_keys;
    };

    SegmentDiskCache() = default;
    explicit SegmentDiskCache(std::string domain_key);

    void set_domain(std::string domain_key);
    const std::string& domain() const { return domain_key_; }
    bool enabled() const { return !domain_key_.empty() && domain_key_ != "debug"; }

    // Persist a closed, BFS-verified segment. Enforces LRU segment cap.
    bool save_segment(int g_seg, int64_t from_ms, int64_t to_ms, int64_t genesis_ms,
                      const std::vector<CachedBlock>& blocks);

    // Load up to max_segments verified entries nearest to g_live (inclusive of live-1 etc.).
    LoadResult load_recent(int g_live, int max_segments,
                           std::vector<CachedBlock>& out_blocks) const;

    // Chunk from_ms keys that cover [from_ms, to_ms) with kChunkMs (newest-first).
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

    static std::string block_to_json_(const CachedBlock& b);
    static bool        json_to_block_(const std::string& json, CachedBlock& out);

    std::string domain_key_;
};
