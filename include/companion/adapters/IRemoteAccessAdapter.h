#pragma once

#include <string>

#include "companion/models/RemoteAccessState.h"

namespace companion::adapters {

class IRemoteAccessAdapter {
public:
    virtual ~IRemoteAccessAdapter() = default;

    virtual models::RemoteAccessState currentState() const = 0;
    virtual bool ensureEnabled(const std::string& username, const std::string& password) = 0;
    virtual bool verifyReadiness() = 0;
};

}  // namespace companion::adapters
