#pragma once

#include "companion/models/ActivitySnapshot.h"

namespace companion::adapters {

class IAppTrackerAdapter {
public:
    virtual ~IAppTrackerAdapter() = default;

    virtual models::ActivitySnapshot snapshot() const = 0;
    virtual std::vector<models::InstalledAppEntry> installedApps() const = 0;
};

}  // namespace companion::adapters

