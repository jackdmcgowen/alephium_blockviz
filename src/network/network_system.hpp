#pragma once

// Network system: curl lifecycle + Alephium poll/fetch (no Vulkan).
#include "engine/blockviz_engine_api.hpp"
#include "domain/block_scene.hpp"

#include <memory>

class INetworkSystem
{
public:
    virtual ~INetworkSystem() = default;
    virtual void start(const NetworkSystemConfig& cfg) = 0;
    virtual void stop() = 0;
};

// scene and engine must outlive the network system.
INetworkSystem* create_network_system(BlockScene& scene, IBlockvizEngine& engine);
void destroy_network_system(INetworkSystem* net);
