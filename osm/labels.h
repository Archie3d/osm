#pragma once

#include "result.h"
#include <blend2d/blend2d.h>
#include <string>
#include <vector>

namespace osm {

struct Label
{
    std::string text;
    BLPoint anchor;
    int priority{};
    float size{12};
    uint32_t color{0xFF45483E};
};

// Draw horizontal UTF-8 labels with halos, suppressing overlaps and duplicate
// names. Returns the number placed; placement is deterministic and preserves state.
Result<std::size_t> renderLabels(BLContext& context, std::vector<Label> labels);

} // namespace osm
