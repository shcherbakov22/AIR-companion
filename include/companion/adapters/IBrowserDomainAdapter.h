#pragma once

#include <optional>
#include <string>

namespace companion::adapters {

class IBrowserDomainAdapter {
public:
    virtual ~IBrowserDomainAdapter() = default;

    virtual std::optional<std::string> activeDomain() const = 0;
};

}  // namespace companion::adapters

