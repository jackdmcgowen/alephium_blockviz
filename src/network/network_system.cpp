#include "network/network_system.hpp"

#include "network/alephium/network_poller.hpp"
#include "network/network_domain.hpp"
#include "domain/alph_block.hpp"

#include <atomic>
#include <curl/curl.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

namespace
{
class NetworkSystem final : public INetworkSystem
{
public:
    NetworkSystem(BlockScene& scene, IEngine& engine)
        : scene_(scene)
        , engine_(engine)
        , poller_(scene, engine)
    {
    }

    ~NetworkSystem() override { free(); }

    const char* name() const override { return "NetworkSystem"; }

    void configure(const NetworkSystemConfig& cfg) override
    {
        std::lock_guard<std::mutex> lock(mu_);
        cfg_ = cfg;
        domain_ = cfg.domain;
        configured_ = true;
        std::printf("[net] NetworkSystem configure url=%s domain=%d\n", cfg_.base_url.c_str(),
                    domain_);
    }

    void init() override
    {
        if (inited_)
            return;
        if (!configured_)
        {
            std::printf("[net] NetworkSystem::init: call configure() first\n");
            return;
        }
        if (!curl_global_ready_)
        {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            curl_global_ready_ = true;
        }
        inited_ = true;
        std::printf("[net] NetworkSystem init url=%s\n", cfg_.base_url.c_str());
    }

    void free() override
    {
        stop();
        if (curl_global_ready_)
        {
            curl_global_cleanup();
            curl_global_ready_ = false;
        }
        inited_ = false;
    }

    void start() override
    {
        if (running_)
            return;
        if (!inited_)
        {
            std::printf("[net] NetworkSystem start: call init() first\n");
            return;
        }

        NetworkPoller::Config net_cfg;
        net_cfg.base_url = cfg_.base_url;
        net_cfg.lookback_ms = cfg_.lookback_ms;
        net_cfg.poll_interval_ms = cfg_.poll_interval_ms;
        poller_.set_domain_meta(domain_, cfg_.base_url);
        poller_.start(net_cfg);
        running_ = true;
        std::printf("[net] NetworkSystem started url=%s\n", cfg_.base_url.c_str());
    }

    void stop() override
    {
        if (running_)
        {
            poller_.stop();
            running_ = false;
        }
    }

    bool switch_domain(int domain, const std::string& base_url) override
    {
        if (domain == static_cast<int>(NetworkDomain::Debug))
            return false;
        if (base_url.empty())
            return false;

        std::lock_guard<std::mutex> lock(mu_);
        if (domain == domain_ && base_url == cfg_.base_url)
            return true;

        switching_ = true;
        // Publish switching HUD so UI can show status immediately.
        {
            BlockScene::NetworkHud hud = scene_.network_hud();
            hud.domain = domain;
            hud.status = static_cast<int>(NetworkStatus::Switching);
            hud.switching = 1;
            std::snprintf(hud.base_url, sizeof(hud.base_url), "%.159s", base_url.c_str());
            scene_.set_network_hud(hud);
        }

        const bool was_running = running_;
        if (was_running)
        {
            poller_.stop();
            running_ = false;
        }

        engine_.clear_selection();
        scene_.reset();

        cfg_.base_url = base_url;
        cfg_.domain = domain;
        domain_ = domain;

        poller_.set_domain_meta(domain_, cfg_.base_url);
        poller_.prepare_domain_switch();

        if (was_running || inited_)
        {
            NetworkPoller::Config net_cfg;
            net_cfg.base_url = cfg_.base_url;
            net_cfg.lookback_ms = cfg_.lookback_ms;
            net_cfg.poll_interval_ms = cfg_.poll_interval_ms;
            poller_.start(net_cfg);
            running_ = true;
        }

        switching_ = false;
        std::printf("[net] domain switch → %d url=%s\n", domain_, cfg_.base_url.c_str());
        return true;
    }

    int domain() const override
    {
        std::lock_guard<std::mutex> lock(mu_);
        return domain_;
    }

    bool is_switching() const override
    {
        return switching_.load();
    }

    std::string base_url() const override
    {
        std::lock_guard<std::mutex> lock(mu_);
        return cfg_.base_url;
    }

private:
    BlockScene& scene_;
    IEngine& engine_;
    NetworkPoller poller_;
    NetworkSystemConfig cfg_{};
    int domain_ = 0;
    mutable std::mutex mu_;
    std::atomic<bool> switching_{ false };
    bool configured_ = false;
    bool inited_ = false;
    bool running_ = false;
    bool curl_global_ready_ = false;
};
} // namespace

INetworkSystem* create_network_system(BlockScene& scene, IEngine& engine)
{
    return new NetworkSystem(scene, engine);
}

void destroy_network_system(INetworkSystem* net)
{
    delete net;
}
