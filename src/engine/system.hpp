#pragma once

// Shared subsystem lifecycle. Typed init(Config) lives on specialized interfaces.
// No Vulkan / curl here.

class ISystem
{
public:
    virtual ~ISystem() = default;

    virtual const char* name() const = 0;
    virtual void shutdown() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};
