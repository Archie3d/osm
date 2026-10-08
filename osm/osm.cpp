#include "osm.h"
#include "labels.h"
#include "style.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <pugixml.hpp>
#include <string_view>
#include <unordered_set>

namespace {

template <typename T>
bool parseNumber(pugi::xml_attribute attribute, T& value)
{
    std::string_view text = attribute.value();
    if (text.empty()) {
        return false;
    }
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool validRegion(const osm::Map::Region& region)
{
    return std::isfinite(region.min_lat) && std::isfinite(region.max_lat)
        && std::isfinite(region.min_lon) && std::isfinite(region.max_lon)
        && region.min_lat >= -90 && region.max_lat <= 90
        && region.min_lon >= -180 && region.max_lon <= 180
        && region.min_lat <= region.max_lat && region.min_lon <= region.max_lon;
}

using Tags = std::unordered_map<std::string, std::string>;

Tags readTags(pugi::xml_node element)
{
    Tags tags;
    for (auto tag : element.children("tag")) {
        tags[tag.attribute("k").value()] = tag.attribute("v").value();
    }
    return tags;
}

} // namespace

namespace osm {

std::string_view tagValue(const Tags& tags, const char* key)
{
    auto found = tags.find(key);
    return found == tags.end() ? std::string_view{} : found->second;
}


Map::Map() = default;
Map::~Map() = default;

const Map::Region& Map::getRegion() const
{
    return m_region;
}

const std::vector<std::string>& Map::getWarnings() const
{
    return m_warnings;
}

Result<Map> Map::load(const std::filesystem::path& filePath)
{
    pugi::xml_document document;
    auto parsed = document.load_file(filePath.c_str());
    if (!parsed) {
        return std::unexpected("Cannot load '" + filePath.string() + "': " + parsed.description());
    }
    auto root = document.document_element();
    if (std::string_view(root.name()) != "osm" || root.next_sibling()) {
        return std::unexpected("Expected a single <osm> root element");
    }

    Map map;
    auto bounds = root.child("bounds");
    if (bounds) {
        if (bounds.next_sibling("bounds")
            || !parseNumber(bounds.attribute("minlat"), map.m_region.min_lat)
            || !parseNumber(bounds.attribute("minlon"), map.m_region.min_lon)
            || !parseNumber(bounds.attribute("maxlat"), map.m_region.max_lat)
            || !parseNumber(bounds.attribute("maxlon"), map.m_region.max_lon)
            || !validRegion(map.m_region)) {
            return std::unexpected("Invalid OSM bounds");
        }
    }

    for (auto element : root.children("node")) {
        std::int64_t id{};
        Node node{};
        if (!parseNumber(element.attribute("id"), id) || id == 0
            || !parseNumber(element.attribute("lat"), node.latitude)
            || !parseNumber(element.attribute("lon"), node.longitude)
            || !std::isfinite(node.latitude) || !std::isfinite(node.longitude)
            || node.latitude < -90 || node.latitude > 90
            || node.longitude < -180 || node.longitude > 180) {
            return std::unexpected("Invalid node ID or coordinates");
        }
        node.tags = readTags(element);
        if (!map.m_nodes.emplace(id, node).second) {
            return std::unexpected("Duplicate node ID: " + std::to_string(id));
        }
        if (!bounds) {
            if (map.m_nodes.size() == 1) {
                map.m_region = {node.latitude, node.longitude, node.latitude, node.longitude};
            } else {
                map.m_region.min_lat = std::min(map.m_region.min_lat, node.latitude);
                map.m_region.max_lat = std::max(map.m_region.max_lat, node.latitude);
                map.m_region.min_lon = std::min(map.m_region.min_lon, node.longitude);
                map.m_region.max_lon = std::max(map.m_region.max_lon, node.longitude);
            }
        }
    }

    std::unordered_map<std::int64_t, std::size_t> wayIndices;
    for (auto element : root.children("way")) {
        std::int64_t id{};
        if (!parseNumber(element.attribute("id"), id) || id == 0 || !wayIndices.emplace(id, map.m_ways.size()).second) {
            return std::unexpected("Invalid or duplicate way ID");
        }
        std::vector<std::int64_t> references;
        for (auto reference : element.children("nd")) {
            std::int64_t nodeId{};
            if (!parseNumber(reference.attribute("ref"), nodeId) || nodeId == 0) {
                return std::unexpected("Invalid node reference in way " + std::to_string(id));
            }
            if (!map.m_nodes.contains(nodeId)) {
                return std::unexpected("Unresolved node reference: " + std::to_string(nodeId));
            }
            references.push_back(nodeId);
        }
        if (references.size() < 2) {
            return std::unexpected("Way must contain at least two node references");
        }
        map.m_ways.push_back({std::move(references), readTags(element)});
    }

    for (auto relation : root.children("relation")) {
        auto tags = readTags(relation);
        if (tagValue(tags, "type") != "multipolygon") {
            continue;
        }
        Area area;
        area.tags = std::move(tags);
        std::vector<std::size_t> members;
        bool complete = true;
        bool hasOuter = false;
        // Assemble each role independently; member order and direction are arbitrary.
        for (const std::string_view role : {"outer", "inner"}) {
            std::vector<std::vector<std::int64_t>> pieces;
            for (auto member : relation.children("member")) {
                auto memberRole = std::string_view(member.attribute("role").value());
                if (memberRole.empty()) {
                    memberRole = "outer";
                }
                if (memberRole != role) {
                    continue;
                }
                std::int64_t reference{};
                if (std::string_view(member.attribute("type").value()) != "way"
                    || !parseNumber(member.attribute("ref"), reference)) {
                    complete = false;
                    continue;
                }
                auto found = wayIndices.find(reference);
                if (found == wayIndices.end()) {
                    complete = false;
                    continue;
                }
                auto& way = map.m_ways[found->second];
                way.relationMember = true;
                if (role == "outer") {
                    members.push_back(found->second);
                }
                pieces.push_back(way.nodes);
            }
            while (!pieces.empty()) {
                auto ring = std::move(pieces.back());
                pieces.pop_back();
                while (ring.front() != ring.back()) {
                    auto next = std::find_if(pieces.begin(), pieces.end(), [&](const auto& piece) {
                        return piece.front() == ring.back() || piece.back() == ring.back();
                    });
                    if (next == pieces.end()) {
                        complete = false;
                        break;
                    }
                    if (next->back() == ring.back()) {
                        std::reverse(next->begin(), next->end());
                    }
                    ring.insert(ring.end(), next->begin() + 1, next->end());
                    pieces.erase(next);
                }
                if (ring.front() == ring.back() && ring.size() >= 4) {
                    area.rings.push_back(std::move(ring));
                    hasOuter |= role == "outer";
                } else {
                    complete = false;
                }
            }
        }
        if (!complete || !hasOuter) {
            map.m_warnings.push_back("Skipped incomplete multipolygon " + std::string(relation.attribute("id").value()));
            continue;
        }
        area.m_style = styleFor(area.tags);
        if (area.m_style.fill != 0) {
            for (auto member : members) {
                map.m_ways[member].areaMember = true;
            }
            map.m_areas.push_back(std::move(area));
        }
    }
    if (map.m_nodes.empty()) {
        return std::unexpected("Map contains no usable nodes");
    }
    map.prepareRenderData();
    return map;
}

Map::LabelMetadata Map::labelMetadata(const Tags& tags, int priority)
{
    LabelMetadata metadata;
    metadata.m_name = tagValue(tags, "name");
    metadata.m_priority = priority;
    metadata.m_size = priority < 2 ? 14.0f : 12.0f;
    if (!tagValue(tags, "waterway").empty() || tagValue(tags, "natural") == "water") {
        metadata.m_color = 0xFF316985;
    }
    return metadata;
}

bool Map::smallAreaLod(const Tags& tags)
{
    // Roads and waterways retain their deliberately exaggerated stroke widths.
    if (!tagValue(tags, "highway").empty() || !tagValue(tags, "waterway").empty()) {
        return false;
    }
    const auto building = tagValue(tags, "building");
    return (!building.empty() && building != "no") || !tagValue(tags, "landuse").empty();
}

void Map::prepareRenderData()
{
    m_fills.clear();
    m_lines.clear();
    m_nodeLabels.clear();
    struct OrderedFill
    {
        Fill m_feature;
        int m_layer;
        double m_area;
    };
    std::vector<OrderedFill> fills;
    auto extendBounds = [&](Region& bounds, const std::vector<std::int64_t>& ring) {
        for (auto id : ring) {
            const auto& node = m_nodes.at(id);
            bounds.min_lat = std::min(bounds.min_lat, node.latitude);
            bounds.max_lat = std::max(bounds.max_lat, node.latitude);
            bounds.min_lon = std::min(bounds.min_lon, node.longitude);
            bounds.max_lon = std::max(bounds.max_lon, node.longitude);
        }
    };
    auto ringArea = [&](const std::vector<std::int64_t>& ring) {
        double area = 0;
        const auto& origin = m_nodes.at(ring.front());
        for (std::size_t i = 1; i < ring.size(); ++i) {
            const auto& a = m_nodes.at(ring[i - 1]);
            const auto& b = m_nodes.at(ring[i]);
            area += (a.longitude - origin.longitude) * (b.latitude - origin.latitude)
                - (b.longitude - origin.longitude) * (a.latitude - origin.latitude);
        }
        return std::abs(area);
    };
    for (std::size_t i = 0; i < m_ways.size(); ++i) {
        auto& way = m_ways[i];
        way.m_style = styleFor(way.tags);
        way.m_bounds = {90, 180, -90, -180};
        extendBounds(way.m_bounds, way.nodes);
        way.m_closed = way.nodes.size() >= 4 && way.nodes.front() == way.nodes.back();
        way.m_label = labelMetadata(way.tags, way.m_closed ? 3 : 4);
        const bool highway = !tagValue(way.tags, "highway").empty();
        const bool filled = way.m_style.fill != 0 && way.m_closed;
        way.m_smallAreaLod = filled && smallAreaLod(way.tags);
        const bool stroke = !(way.relationMember || filled) || highway || !tagValue(way.tags, "waterway").empty();
        way.m_casing = highway && way.m_style.width >= 3;
        if (filled && !way.areaMember) {
            fills.push_back({{i, false}, way.m_style.layer, ringArea(way.nodes)});
        }
        if (stroke) {
            m_lines.push_back(i);
        }
    }
    for (std::size_t i = 0; i < m_areas.size(); ++i) {
        auto& area = m_areas[i];
        area.m_bounds = {90, 180, -90, -180};
        area.m_label = labelMetadata(area.tags, 3);
        area.m_smallAreaLod = smallAreaLod(area.tags);
        double size = 0;
        for (const auto& ring : area.rings) {
            extendBounds(area.m_bounds, ring);
            size += ringArea(ring);
        }
        fills.push_back({{i, true}, area.m_style.layer, size});
    }
    // Preserve source order on equal keys: ways precede relation areas.
    std::stable_sort(fills.begin(), fills.end(), [](const OrderedFill& left, const OrderedFill& right) {
        return left.m_layer == right.m_layer ? left.m_area > right.m_area : left.m_layer < right.m_layer;
    });
    for (const auto& fill : fills) {
        m_fills.push_back(fill.m_feature);
    }
    std::stable_sort(m_lines.begin(), m_lines.end(), [&](std::size_t left, std::size_t right) {
        return m_ways[left].m_style.layer < m_ways[right].m_style.layer;
    });
    for (const auto& [id, node] : m_nodes) {
        if (!tagValue(node.tags, "name").empty()) {
            const int priority = !tagValue(node.tags, "place").empty() ? 0
                : tagValue(node.tags, "natural") == "peak" ? 1 : 2;
            m_nodeLabels.push_back({id, labelMetadata(node.tags, priority)});
        }
    }
}

Result<void> Map::render(BLContext& ctx, const Region& region)
{
    return render(ctx, region, RenderOptions{});
}

Result<void> Map::render(BLContext& ctx, const Region& region,
    const RenderOptions& options, RenderStats* stats)
{
    if (stats) {
        *stats = {};
    }
    if (!std::isfinite(options.m_minAreaSizePixels) || options.m_minAreaSizePixels < 0) {
        return std::unexpected("LOD threshold must be finite and nonnegative");
    }
    if (!validRegion(region) || region.min_lat == region.max_lat || region.min_lon == region.max_lon) {
        return std::unexpected("Rendering requires finite, ordered bounds with nonzero latitude and longitude spans");
    }
    const auto size = ctx.target_size();
    if (size.w <= 0 || size.h <= 0) {
        return std::unexpected("Rendering requires an active image context");
    }
    if (ctx.save() != BL_SUCCESS) {
        return std::unexpected("Cannot save Blend2D context");
    }
    BLResult status = ctx.reset_transform();
    status |= ctx.clip_to_rect(0, 0, size.w, size.h);
    status |= ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
    status |= ctx.set_stroke_width(2.0);
    status |= ctx.set_stroke_style(BLRgba32(0xFF263E52));

    auto project = [&](double longitude, double latitude) {
        return BLPoint((longitude - region.min_lon) / (region.max_lon - region.min_lon) * size.w,
            (region.max_lat - latitude) / (region.max_lat - region.min_lat) * size.h);
    };
    // Clip filled rings as polygons, not as independent line segments. This
    // also fills a viewport completely contained inside an offscreen polygon.
    auto appendRing = [&](BLPath& path, const std::vector<std::int64_t>& ring) {
        std::vector<BLPoint> points;
        for (auto id : ring) {
            const auto& node = m_nodes.at(id);
            points.emplace_back(node.longitude, node.latitude);
        }
        for (int edge = 0; edge < 4 && !points.empty(); ++edge) {
            const bool vertical = edge < 2;
            const bool lower = edge % 2 == 0;
            const double bound = vertical ? (lower ? region.min_lon : region.max_lon)
                : (lower ? region.min_lat : region.max_lat);
            auto coordinate = [&](const BLPoint& point) { return vertical ? point.x : point.y; };
            auto inside = [&](const BLPoint& point) {
                return lower ? coordinate(point) >= bound : coordinate(point) <= bound;
            };
            std::vector<BLPoint> clipped;
            auto previous = points.back();
            for (const auto& current : points) {
                if (inside(current) != inside(previous)) {
                    const double t = (bound - coordinate(previous)) / (coordinate(current) - coordinate(previous));
                    clipped.emplace_back(vertical ? bound : previous.x + t * (current.x - previous.x),
                        vertical ? previous.y + t * (current.y - previous.y) : bound);
                }
                if (inside(current)) {
                    clipped.push_back(current);
                }
                previous = current;
            }
            points = std::move(clipped);
        }
        if (points.size() < 3) {
            return;
        }
        status |= path.move_to(project(points.front().x, points.front().y));
        for (std::size_t index = 1; index < points.size(); ++index) {
            status |= path.line_to(project(points[index].x, points[index].y));
        }
        status |= path.close();
    };
    status |= ctx.set_fill_rule(BL_FILL_RULE_EVEN_ODD);
    auto visible = [&](const Region& bounds) {
        return bounds.min_lon <= region.max_lon && bounds.max_lon >= region.min_lon
            && bounds.min_lat <= region.max_lat && bounds.max_lat >= region.min_lat;
    };
    auto culled = [&](const Region& bounds, bool eligible) {
        // Use full feature bounds, not their intersection with the viewport.
        // Keep every ring of a surviving multipolygon, including tiny holes.
        return eligible && options.m_minAreaSizePixels > 0
            && (bounds.max_lon - bounds.min_lon) / (region.max_lon - region.min_lon) * size.w < options.m_minAreaSizePixels
            && (bounds.max_lat - bounds.min_lat) / (region.max_lat - region.min_lat) * size.h < options.m_minAreaSizePixels;
    };
    for (const auto& fill : m_fills) {
        const auto& bounds = fill.m_isArea ? m_areas[fill.m_index].m_bounds : m_ways[fill.m_index].m_bounds;
        const bool eligible = fill.m_isArea ? m_areas[fill.m_index].m_smallAreaLod : m_ways[fill.m_index].m_smallAreaLod;
        if (stats) {
            ++stats->fillsConsidered;
        }
        if (!visible(bounds)) {
            if (stats) {
                ++stats->fillsOffscreen;
            }
            continue;
        }
        if (culled(bounds, eligible)) {
            if (stats) {
                ++stats->fillsCulled;
            }
            continue;
        }
        if (stats) {
            ++stats->fillsDrawn;
        }
        BLPath path;
        if (fill.m_isArea) {
            const auto& area = m_areas[fill.m_index];
            for (const auto& ring : area.rings) {
                appendRing(path, ring);
            }
            status |= ctx.fill_path(path, BLRgba32(area.m_style.fill));
        } else {
            const auto& way = m_ways[fill.m_index];
            appendRing(path, way.nodes);
            status |= ctx.fill_path(path, BLRgba32(way.m_style.fill));
        }
    }
    for (auto index : m_lines) {
        const auto* feature = &m_ways[index];
        if (!visible(feature->m_bounds) || culled(feature->m_bounds, feature->m_smallAreaLod)) {
            continue;
        }
        const auto& way = feature->nodes;
        const auto& style = feature->m_style;
        status |= ctx.set_stroke_width(style.width);
        status |= ctx.set_stroke_style(BLRgba32(style.stroke));
        BLPath path;
        BLPoint previousEnd{};
        bool connected = false;
        for (std::size_t index = 1; index < way.size(); ++index) {
            const auto& start = m_nodes.at(way[index - 1]);
            const auto& end = m_nodes.at(way[index]);
            const double dx = end.longitude - start.longitude;
            const double dy = end.latitude - start.latitude;
            double first = 0;
            double last = 1;
            // Liang-Barsky clipping keeps even very small viewports numerically bounded.
            auto clip = [&](double direction, double distance) {
                if (direction == 0) {
                    return distance >= 0;
                }
                const double ratio = distance / direction;
                if (direction < 0) {
                    first = std::max(first, ratio);
                } else {
                    last = std::min(last, ratio);
                }
                return first <= last;
            };
            if (!clip(-dx, start.longitude - region.min_lon)
                || !clip(dx, region.max_lon - start.longitude)
                || !clip(-dy, start.latitude - region.min_lat)
                || !clip(dy, region.max_lat - start.latitude)) {
                connected = false;
                continue;
            }
            auto clippedPoint = [&](double fraction) {
                return project(std::clamp(start.longitude + fraction * dx, region.min_lon, region.max_lon),
                    std::clamp(start.latitude + fraction * dy, region.min_lat, region.max_lat));
            };
            const auto p0 = clippedPoint(first);
            const auto p1 = clippedPoint(last);
            if (!connected || p0.x != previousEnd.x || p0.y != previousEnd.y) {
                status |= path.move_to(p0);
            }
            status |= path.line_to(p1);
            previousEnd = p1;
            connected = true;
        }
        if (feature->m_casing) {
            status |= ctx.set_stroke_width(style.width + 1.5);
            status |= ctx.stroke_path(path, BLRgba32(0xFFB6ADA0));
            status |= ctx.set_stroke_width(style.width);
        }
        status |= ctx.stroke_path(path);
    }
    std::vector<Label> labels;
    auto addLabel = [&](const LabelMetadata& metadata, BLPoint anchor) {
        if (metadata.m_name.empty() || anchor.x < 0 || anchor.y < 0 || anchor.x > size.w || anchor.y > size.h) {
            return;
        }
        labels.push_back({metadata.m_name, anchor, metadata.m_priority, metadata.m_size, metadata.m_color});
    };
    for (const auto& candidate : m_nodeLabels) {
        const auto& node = m_nodes.at(candidate.m_id);
        if (node.longitude >= region.min_lon && node.longitude <= region.max_lon
            && node.latitude >= region.min_lat && node.latitude <= region.max_lat) {
            addLabel(candidate.m_label, project(node.longitude, node.latitude));
        }
    }
    // Find an interior anchor using scanlines through the visible area. Even-odd
    // intersections exclude holes, including holes crossing viewport edges.
    auto areaAnchor = [&](const std::vector<const std::vector<std::int64_t>*>& rings) {
        double minY = size.h;
        double maxY = 0;
        for (const auto* ring : rings) {
            for (auto id : *ring) {
                const auto& node = m_nodes.at(id);
                const double y = project(node.longitude, node.latitude).y;
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
        minY = std::max(0.0, minY);
        maxY = std::min(size.h, maxY);
        BLPoint best(-1, -1);
        double bestWidth = 0;
        if (minY >= maxY) {
            return best;
        }
        for (double fraction : {0.5, 0.35, 0.65}) {
            const double y = minY + fraction * (maxY - minY);
            std::vector<double> intersections;
            for (const auto* ring : rings) {
                for (std::size_t i = 1; i < ring->size(); ++i) {
                    const auto& a = m_nodes.at((*ring)[i - 1]);
                    const auto& b = m_nodes.at((*ring)[i]);
                    const auto p = project(a.longitude, a.latitude);
                    const auto q = project(b.longitude, b.latitude);
                    if ((p.y > y) != (q.y > y)) {
                        intersections.push_back(p.x + (y - p.y) / (q.y - p.y) * (q.x - p.x));
                    }
                }
            }
            std::sort(intersections.begin(), intersections.end());
            for (std::size_t i = 1; i < intersections.size(); i += 2) {
                const double left = std::max(0.0, intersections[i - 1]);
                const double right = std::min(size.w, intersections[i]);
                if (right - left > bestWidth) {
                    bestWidth = right - left;
                    best = BLPoint((left + right) / 2, y);
                }
            }
        }
        return best;
    };
    for (const auto& way : m_ways) {
        if (way.m_label.m_name.empty() || way.areaMember || !visible(way.m_bounds)
            || culled(way.m_bounds, way.m_smallAreaLod)) {
            continue;
        }
        if (way.m_closed) {
            addLabel(way.m_label, areaAnchor({&way.nodes}));
            continue;
        }
        // Candidate midpoints along visible segments let a road label survive
        // when its endpoints (or another segment with the same name) are offscreen.
        for (std::size_t i = 1; i < way.nodes.size(); ++i) {
            const auto& a = m_nodes.at(way.nodes[i - 1]);
            const auto& b = m_nodes.at(way.nodes[i]);
            const auto p = project(a.longitude, a.latitude);
            const auto q = project(b.longitude, b.latitude);
            double first = 0;
            double last = 1;
            auto clip = [&](double direction, double distance) {
                if (direction == 0) {
                    return distance >= 0;
                }
                const double ratio = distance / direction;
                if (direction < 0) {
                    first = std::max(first, ratio);
                } else {
                    last = std::min(last, ratio);
                }
                return first <= last;
            };
            const double dx = q.x - p.x;
            const double dy = q.y - p.y;
            if (clip(-dx, p.x) && clip(dx, size.w - p.x)
                && clip(-dy, p.y) && clip(dy, size.h - p.y)) {
                const double middle = (first + last) / 2;
                addLabel(way.m_label, BLPoint(p.x + middle * dx, p.y + middle * dy));
            }
        }
    }
    for (const auto& area : m_areas) {
        if (area.m_label.m_name.empty() || !visible(area.m_bounds)
            || culled(area.m_bounds, area.m_smallAreaLod)) {
            continue;
        }
        std::vector<const std::vector<std::int64_t>*> rings;
        for (const auto& ring : area.rings) {
            rings.push_back(&ring);
        }
        addLabel(area.m_label, areaAnchor(rings));
    }
    auto labelResult = renderLabels(ctx, std::move(labels));
    status |= ctx.flush(BL_CONTEXT_FLUSH_SYNC);
    status |= ctx.restore();
    if (!labelResult) {
        return std::unexpected(labelResult.error());
    }
    if (status != BL_SUCCESS) {
        return std::unexpected("Blend2D failed to render map ways");
    }
    return {};
}

} // namespace osm
