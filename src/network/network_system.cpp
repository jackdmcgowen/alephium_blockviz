#include "network/network_system.hpp"

#include "adapters/alephium/network_poller.hpp"
#include "alph_block.hpp"

#include <curl/curl.h>
#include <cstdio>
#include <memory>

namespace
{
class NetworkSystem final : public INetworkSystem
{
public:
    NetworkSystem(BlockScene& scene, IBlockvizEngine& engine)
        : scene_(scene)
        , engine_(engine)
        , poller_(scene, engine)
    {
    }

    ~NetworkSystem() override { stop(); }

    void start(const NetworkSystemConfig& cfg) override
    {
        stop();
        if (!curl_global_ready_)
        {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            curl_global_ready_ = true;
        }

        NetworkPoller::Config net_cfg;
        net_cfg.base_url = cfg.base_url;
        net_cfg.lookback_ms = cfg.lookback_ms;
        net_cfg.poll_interval_ms = cfg.poll_interval_ms;
        poller_.start(net_cfg);
        running_ = true;
        std::printf("[net] NetworkSystem started url=%s\n", cfg.base_url.c_str());
    }

    void stop() override
    {
        if (running_)
        {
            poller_.stop();
            running_ = false;
        }
        if (curl_global_ready_)
        {
            curl_global_cleanup();
            curl_global_ready_ = false;
        }
    }

private:
    BlockScene& scene_;
    IBlockvizEngine& engine_;
    NetworkPoller poller_;
    bool running_ = false;
    bool curl_global_ready_ = false;
};
} // namespace

INetworkSystem* create_network_system(BlockScene& scene, IBlockvizEngine& engine)
{
    return new NetworkSystem(scene, engine);
}

void destroy_network_system(INetworkSystem* net)
{
    delete net;
}
