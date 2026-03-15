#include "companion/adapters/windows/WindowsAdapters.h"

namespace companion::adapters::windows {

bool WindowsServiceLifecycleAdapter::install() {
    return true;
}

bool WindowsServiceLifecycleAdapter::start() {
    return true;
}

bool WindowsServiceLifecycleAdapter::stop() {
    return true;
}

}  // namespace companion::adapters::windows

