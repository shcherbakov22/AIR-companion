#pragma once

namespace companion::adapters {

class IServiceLifecycleAdapter {
public:
    virtual ~IServiceLifecycleAdapter() = default;

    virtual bool install() = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
};

}  // namespace companion::adapters

