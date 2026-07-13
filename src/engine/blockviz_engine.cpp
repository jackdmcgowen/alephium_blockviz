#include "engine/blockviz_engine_api.hpp"

#include "network/network_system.hpp"

#include <cstdio>

// Product engine shell: owns GraphicsSystem + NetworkSystem. No Vulkan/curl here.
namespace
{
class BlockvizEngine final : public IBlockvizEngine
{
public:
    BlockvizEngine() = default;

    ~BlockvizEngine() override
    {
        stop();
        if (network_)
        {
            destroy_network_system(network_);
            network_ = nullptr;
        }
        if (graphics_)
        {
            destroy_graphics_system(graphics_);
            graphics_ = nullptr;
        }
    }

    // --- IRenderEngine ---
    void initialize(const EngineCreateInfo& info) override
    {
        if (!graphics_)
            graphics_ = create_graphics_system();
        graphics_->initialize(info);
    }

    void resize(uint32_t width, uint32_t height) override
    {
        if (graphics_)
            graphics_->resize(width, height);
    }

    void shutdown() override
    {
        stop();
        if (graphics_)
            graphics_->shutdown();
    }

    void start() override
    {
        if (graphics_)
            graphics_->start();
    }

    void stop() override
    {
        if (network_)
            network_->stop();
        if (graphics_)
            graphics_->stop();
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

    // --- IBlockvizEngine ---
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
                       const std::vector<std::string>& incomplete_hashes) override
    {
        if (graphics_)
            graphics_->publish_frame(frame, pick_map, confirmed_tip_hashes,
                                     incomplete_hashes);
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

    void start_network(const NetworkSystemConfig& cfg)
    {
        if (!scene_)
        {
            std::printf("[engine] start_network: no scene set\n");
            return;
        }
        if (!network_)
            network_ = create_network_system(*scene_, *this);
        network_->start(cfg);
    }

private:
    IBlockvizEngine* graphics_ = nullptr; // GraphicsSystem (graphics.lib)
    INetworkSystem*  network_  = nullptr;
    BlockScene*      scene_    = nullptr;
};

BlockvizEngine* as_blockviz_engine(IBlockvizEngine* e)
{
    return dynamic_cast<BlockvizEngine*>(e);
}
} // namespace

IBlockvizEngine* create_blockviz_engine()
{
    return new BlockvizEngine();
}

void destroy_blockviz_engine(IBlockvizEngine* engine)
{
    delete engine;
}

void blockviz_engine_start_network(IBlockvizEngine* engine, const NetworkSystemConfig& cfg)
{
    if (auto* be = as_blockviz_engine(engine))
        be->start_network(cfg);
    else
        std::printf("[engine] start_network: not a BlockvizEngine\n");
}
