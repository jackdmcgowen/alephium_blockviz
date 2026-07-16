#include "network/network_system.hpp"

#include "network/alephium/network_poller.hpp"
#include "domain/alph_block.hpp"

#include <curl/curl.h>
#include <cstdio>
#include <memory>

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

    ~NetworkSystem() override { stop(); }

    const char* name() const override { return "NetworkSystem"; }

    void init(const NetworkSystemConfig& cfg) override
    {
        cfg_ = cfg;
        if (!curl_global_ready_)
        {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            curl_global_ready_ = true;
        }
        std::printf("[net] NetworkSystem init url=%s\n", cfg_.base_url.c_str());
    }

    void shutdown() override
    {
        stop();
        if (curl_global_ready_)
        {
            curl_global_cleanup();
            curl_global_ready_ = false;
        }
    }

    void start() override
    {
        if (running_)
            return;
        if (!curl_global_ready_)
        {
            std::printf("[net] NetworkSystem start: call init() first\n");
            return;
        }

        NetworkPoller::Config net_cfg;
        net_cfg.base_url = cfg_.base_url;
        net_cfg.lookback_ms = cfg_.lookback_ms;
        net_cfg.poll_interval_ms = cfg_.poll_interval_ms;
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

private:
    BlockScene& scene_;
    IEngine& engine_;
    NetworkPoller poller_;
    NetworkSystemConfig cfg_{};
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
