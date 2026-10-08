#include "labels.h"
#include "label_font_data.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <unordered_set>

namespace osm {

Result<std::size_t> renderLabels(BLContext& context, std::vector<Label> labels)
{
    if (labels.empty()) {
        return 0;
    }
    BLFontData data;
    BLFontFace face;
    if (data.create_from_data(labelFontData, sizeof(labelFontData)) != BL_SUCCESS
        || face.create_from_data(data, 0) != BL_SUCCESS) {
        return std::unexpected("Cannot load embedded map label font");
    }
    std::sort(labels.begin(), labels.end(), [](const Label& left, const Label& right) {
        return std::tie(left.priority, left.text, left.anchor.y, left.anchor.x)
            < std::tie(right.priority, right.text, right.anchor.y, right.anchor.x);
    });
    if (context.save() != BL_SUCCESS) {
        return std::unexpected("Cannot save label context");
    }
    BLResult status = context.set_stroke_width(3);
    status |= context.set_stroke_join(BL_STROKE_JOIN_ROUND);
    std::vector<BLBox> occupied;
    std::unordered_set<std::string> names;
    const auto size = context.target_size();
    for (const auto& label : labels) {
        if (label.text.empty() || names.contains(label.text)
            || !std::isfinite(label.anchor.x) || !std::isfinite(label.anchor.y)) {
            continue;
        }
        BLFont font;
        BLGlyphBuffer glyphs;
        BLTextMetrics metrics{};
        if (font.create_from_face(face, label.size) != BL_SUCCESS
            || glyphs.set_utf8_text(label.text.data(), label.text.size()) != BL_SUCCESS
            || font.shape(glyphs) != BL_SUCCESS
            || font.get_text_metrics(glyphs, metrics) != BL_SUCCESS) {
            status = BL_ERROR_INVALID_DATA;
            break;
        }
        const auto& bounds = metrics.bounding_box;
        const double width = bounds.x1 - bounds.x0;
        // This Blend2D version supplies horizontal bounds only. Font ascent
        // and descent provide a conservative vertical box including accents.
        const auto& fontMetrics = font.metrics();
        const double height = fontMetrics.ascent + fontMetrics.descent;
        if (width <= 0 || height <= 0) {
            continue;
        }
        const BLPoint origin(label.anchor.x - (bounds.x0 + bounds.x1) / 2,
            label.anchor.y + (fontMetrics.ascent - fontMetrics.descent) / 2);
        constexpr double padding = 4;
        const BLBox box(label.anchor.x - width / 2 - padding, label.anchor.y - height / 2 - padding,
            label.anchor.x + width / 2 + padding, label.anchor.y + height / 2 + padding);
        if (box.x0 < 0 || box.y0 < 0 || box.x1 > size.w || box.y1 > size.h) {
            continue;
        }
        const bool overlaps = std::any_of(occupied.begin(), occupied.end(), [&](const BLBox& other) {
            return box.x0 < other.x1 && box.x1 > other.x0 && box.y0 < other.y1 && box.y1 > other.y0;
        });
        if (overlaps) {
            continue;
        }
        status |= context.stroke_glyph_run(origin, font, glyphs.glyph_run(), BLRgba32(0xFFF9F7EE));
        status |= context.fill_glyph_run(origin, font, glyphs.glyph_run(), BLRgba32(label.color));
        occupied.push_back(box);
        names.insert(label.text);
    }
    status |= context.restore();
    if (status != BL_SUCCESS) {
        return std::unexpected("Blend2D failed to draw map labels");
    }
    return occupied.size();
}

} // namespace osm
