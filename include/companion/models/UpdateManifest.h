#pragma once

#include <cstdint>
#include <string>

namespace companion::models {

struct UpdateManifest {
    bool available{false};
    std::string version;
    std::string channel{"stable"};
    bool mandatory{false};
    std::string downloadUrl;
    std::string sha256;
    std::uint64_t sizeBytes{0};
};

}  // namespace companion::models
