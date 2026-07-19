#include "engine/pch.h"
#include "engine/engine.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

// Product shell: owns registered ISystem components. No Vulkan/curl here.
namespace
{
class BlockVizEngine final : public IEngine
{
public:
    BlockVizEngine() = default;

    ~BlockVizEngine() override
    {
        stop();
        free_systems();
        for (ISystem* s : systems_)
            delete s;
        systems_.clear();
        graphics_ = nullptr;
    }

    void add_system(ISystem* system) override
    {
        if (!system)
            return;
        for (ISystem* s : systems_)
        {
            if (s == system)
                return;
        }
        systems_.push_back(system);
        if (auto* g = dynamic_cast<IGraphicsSystem*>(system))
            graphics_ = g;
        if (auto* n = dynamic_cast<INetworkSystem*>(system))
            network_ = n;
        std::printf("[engine] add_system %s\n", system->name());
    }

    ISystem* find_system(const char* name) const override
    {
        if (!name)
            return nullptr;
        for (ISystem* s : systems_)
        {
            if (s && s->name() && std::strcmp(s->name(), name) == 0)
                return s;
        }
        return nullptr;
    }

    void init_systems() override
    {
        for (ISystem* s : systems_)
        {
            if (s)
            {
                std::printf("[engine] init %s\n", s->name());
                s->init();
            }
        }
    }

    void free_systems() override
    {
        for (auto it = systems_.rbegin(); it != systems_.rend(); ++it)
        {
            ISystem* s = *it;
            if (!s)
                continue;
            s->stop();
            s->free();
        }
    }

    void start() override
    {
        for (ISystem* s : systems_)
        {
            if (s)
                s->start();
        }
    }

    void stop() override
    {
        for (auto it = systems_.rbegin(); it != systems_.rend(); ++it)
        {
            if (*it)
                (*it)->stop();
        }
    }

    void resize(uint32_t width, uint32_t height) override
    {
        if (graphics_)
            graphics_->resize(width, height);
    }

    void submit_frame(const FrameSubmit& frame) override
    {
        if (graphics_)
            graphics_->submit_frame(frame);
    }

    void set_ui_overlay(IUiOverlay* overlay) override
    {
        if (graphics_)
            graphics_->set_ui_overlay(overlay);
    }

    void request_pick(const PickQuery& q) override
    {
        if (graphics_)
            graphics_->request_pick(q);
    }

    bool consume_pick(PickResult& out) override
    {
        return graphics_ ? graphics_->consume_pick(out) : false;
    }

    void set_scene(BlockScene* scene) override
    {
        scene_ = scene;
        if (graphics_)
            graphics_->set_scene(scene);
    }

    void set_camera(CameraController* camera) override
    {
        if (graphics_)
            graphics_->set_camera(camera);
    }

    void set_frame_source(IFrameSource* source) override
    {
        if (graphics_)
            graphics_->set_frame_source(source);
    }

    void set_selection(const std::string& hash) override
    {
        if (graphics_)
            graphics_->set_selection(hash);
    }

    void clear_selection() override
    {
        if (graphics_)
            graphics_->clear_selection();
    }

    bool is_selected(const std::string& hash) const override
    {
        return graphics_ ? graphics_->is_selected(hash) : false;
    }

    AlphBlock copy_selected_block() const override
    {
        return graphics_ ? graphics_->copy_selected_block() : AlphBlock{};
    }

    void set_ui_dep_hover(const std::string& hash) override
    {
        if (graphics_)
            graphics_->set_ui_dep_hover(hash);
    }

    std::string consume_detail_refill_request() override
    {
        return graphics_ ? graphics_->consume_detail_refill_request() : std::string{};
    }

    void publish_ui_snapshot(UiSnapshot snap) override
    {
        if (graphics_)
            graphics_->publish_ui_snapshot(std::move(snap));
    }

    UiSnapshot copy_ui_snapshot() const override
    {
        return graphics_ ? graphics_->copy_ui_snapshot() : UiSnapshot{};
    }

    void publish_frame(const FrameSubmit& frame,
                       const std::vector<std::string>& pick_map,
                       const std::vector<std::string>& confirmed_tip_hashes,
                       const std::vector<std::string>& cyan_frontier_hashes,
                       const std::vector<std::string>& incomplete_hashes) override
    {
        if (graphics_)
            graphics_->publish_frame(frame, pick_map, confirmed_tip_hashes,
                                     cyan_frontier_hashes, incomplete_hashes);
    }

    void init_platform(void* hInstance, void* hwnd) override
    {
        if (graphics_)
            graphics_->init_platform(hInstance, hwnd);
    }

    void on_resize() override
    {
        if (graphics_)
            graphics_->on_resize();
    }

    bool switch_network_domain(int domain, const std::string& base_url) override
    {
        return network_ ? network_->switch_domain(domain, base_url) : false;
    }

    int network_domain() const override
    {
        return network_ ? network_->domain() : 0;
    }

    bool network_is_switching() const override
    {
        return network_ ? network_->is_switching() : false;
    }

private:
    std::vector<ISystem*> systems_;
    IGraphicsSystem* graphics_ = nullptr;
    INetworkSystem*  network_  = nullptr;
    BlockScene* scene_ = nullptr;
};
} // namespace

IEngine* create_engine()
{
    return new BlockVizEngine();
}

void destroy_engine(IEngine* engine)
{
    delete engine;
}
