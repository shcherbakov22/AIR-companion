#pragma once

#include <string>

namespace companion::models {

struct UpdateManifest {
    bool available{false};
    std::string version;
    std::string channel{"stable"};
    bool mandatory{false};
    std::string downloadUrl;
    std::string sha256;
};

}  // namespace companion::models
