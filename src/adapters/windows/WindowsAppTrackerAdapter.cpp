#include "companion/adapters/windows/WindowsAdapters.h"

namespace companion::adapters::windows {

models::ActivitySnapshot WindowsAppTrackerAdapter::snapshot() const {
    models::ActivitySnapshot snapshot;
    snapshot.focusedApp = "stub-app.exe";
    snapshot.focusedWindowTitle = "Stub Window";
    snapshot.openApps = {"stub-app.exe", "browser.exe"};
    return snapshot;
}

}  // namespace companion::adapters::windows

