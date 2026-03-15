#include "companion/adapters/windows/WindowsAdapters.h"

namespace companion::adapters::windows {

std::optional<std::string> WindowsBrowserDomainAdapter::activeDomain() const {
    return std::string{"example.com"};
}

}  // namespace companion::adapters::windows

