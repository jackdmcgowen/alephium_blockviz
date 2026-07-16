#pragma once

// Shared subsystem lifecycle. Typed configure() lives on specialized interfaces;
// init()/free() are polymorphic for every ISystem derivative.
// No Vulkan / curl here.

class ISystem
{
public:
    virtual ~ISystem() = default;

    virtual const char* name() const = 0;

    // Polymorphic lifecycle (same call for graphics, network, future systems).
    virtual void init() = 0;   // prepare resources using prior configure()
    virtual void free() = 0;   // release resources (replaces shutdown)
    virtual void start() = 0;  // threads / pumps
    virtual void stop() = 0;
};
