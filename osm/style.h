#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace osm {

using Tags = std::unordered_map<std::string, std::string>;

std::string_view tagValue(const Tags& tags, const char* key);

struct Style
{
    uint32_t fill{};
    uint32_t stroke{0xFF8B9188};
    double width{0.7};
    int layer{};
};

Style styleFor(const osm::Tags& tags);

} // namespace osm