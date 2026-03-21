#pragma once

#include <string>

#include "companion/models/RemoteAccessState.h"

namespace companion::adapters {

class IRemoteAccessAdapter {
public:
    virtual ~IRemoteAccessAdapter() = default;

    virtual models::RemoteAccessState currentState() const = 0;
    virtual bool startRemoteControl() = 0;
    virtual bool stopRemoteControl() = 0;
    virtual bool verifyReadiness() = 0;
};

}  // namespace companion::adapters
