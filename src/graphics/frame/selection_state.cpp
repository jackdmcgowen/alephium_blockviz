#include "graphics/pch.h"
#include "graphics/graphics_system.hpp"

#include <string>

void GraphicsSystem::request_detail_refill_unlocked(const std::string& hash)
{
    if (hash.empty())
        return;
    std::lock_guard<std::mutex> lock(detail_refill_mutex_);
    detail_refill_hash_ = hash;
}

void GraphicsSystem::pin_and_maybe_refill(const std::string& hash, bool has_txns)
{
    if (!scene_)
        return;
    // Pin selection so prune keeps full payloads for this id.
    scene_->detail_store().set_full_detail_pin(hash);
    if (!hash.empty() && !has_txns)
        request_detail_refill_unlocked(hash);
}

void GraphicsSystem::clear_selection_unlocked()
{
    selected_hash_.clear();
    selected_block = AlphBlock{};
    if (camera_)
        camera_->release_look_aim();
    if (scene_)
        scene_->detail_store().set_full_detail_pin({});
}

void GraphicsSystem::clear_selection()
{
    std::lock_guard<std::mutex> lock(selection_mutex_);
    clear_selection_unlocked();
}

void GraphicsSystem::set_selection_unlocked(const std::string& hash)
{
    if (hash.empty())
    {
        clear_selection_unlocked();
        return;
    }
    if (hash == selected_hash_ && selected_block.hash == hash && !selected_block.txns.empty())
        return;

    selected_hash_ = hash;
    // Prefer detail store only (own mutex) â€” never lock scene from selection path
    // to avoid ABBA deadlock with render_loop (scene then selection).
    if (scene_)
    {
        if (auto d = scene_->detail_store().get(hash))
            selected_block = std::move(*d);
        else
        {
            selected_block = AlphBlock{};
            selected_block.hash = hash;
        }
        pin_and_maybe_refill(hash, !selected_block.txns.empty());
    }
    else
    {
        selected_block = AlphBlock{};
        selected_block.hash = hash;
    }
}

void GraphicsSystem::set_selection(const std::string& hash)
{
    std::lock_guard<std::mutex> lock(selection_mutex_);
    set_selection_unlocked(hash);
}

void GraphicsSystem::set_ui_dep_hover(const std::string& hash)
{
    std::lock_guard<std::mutex> lock(selection_mutex_);
    ui_dep_hover_hash_ = hash;
}

void GraphicsSystem::set_scene_filter_multi_tx(bool enabled)
{
    std::lock_guard<std::mutex> lock(selection_mutex_);
    filter_multi_tx_ = enabled;
}

void GraphicsSystem::set_scene_filter_min_alph(double min_alph)
{
    std::lock_guard<std::mutex> lock(selection_mutex_);
    filter_min_alph_ = (min_alph > 0.0) ? min_alph : 0.0;
}

std::string GraphicsSystem::consume_detail_refill_request()
{
    std::lock_guard<std::mutex> lock(detail_refill_mutex_);
    std::string out = std::move(detail_refill_hash_);
    detail_refill_hash_.clear();
    return out;
}

bool GraphicsSystem::is_selected(const std::string& hash) const
{
    std::lock_guard<std::mutex> lock(selection_mutex_);
    return !hash.empty() && selected_hash_ == hash;
}

AlphBlock GraphicsSystem::copy_selected_block() const
{
    std::lock_guard<std::mutex> lock(selection_mutex_);
    return selected_block;
}

void GraphicsSystem::refresh_selection_if_needed(BlockScene& scene)
{
    // Caller holds scene.mutex() and selection_mutex_.
    if (selected_hash_.empty())
        return;
    if (selected_block.hash != selected_hash_ || selected_block.txns.empty())
    {
        selected_block = scene.resolve_detail(selected_hash_);
        // If still slim after store catch-up, re-request network rehydrate (PR11).
        if (selected_block.txns.empty())
            pin_and_maybe_refill(selected_hash_, false);
        else
            scene.detail_store().set_full_detail_pin(selected_hash_);
    }
}

