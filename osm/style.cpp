#include <string_view>
#include "style.h"

namespace osm {

Style styleFor(const Tags& tags)
{
    Style style;
    auto natural = tagValue(tags, "natural");
    auto landuse = tagValue(tags, "landuse");
    auto building = tagValue(tags, "building");
    auto highway = tagValue(tags, "highway");

    if (!landuse.empty()) {
        style.fill = 0xFFE4E4CF;
        if (landuse == "meadow" || landuse == "grass" || landuse == "village_green") {
            style.fill = 0xFFCDE2B5;
        } else if (landuse == "forest") {
            style.fill = 0xFFA8C99A;
        } else if (landuse == "residential") {
            style.fill = 0xFFE3DDD5;
        } else if (landuse == "farmland" || landuse == "farmyard") {
            style.fill = 0xFFE9DFC1;
        } else if (landuse == "reservoir") {
            style.fill = 0xFFA3CEE5;
        }
    }

    if (natural == "wood" || natural == "scrub") {
        style.fill = natural == "wood" ? 0xFFA8C99A : 0xFFBFD1A6;
    } else if (natural == "heath" || natural == "grassland") {
        style.fill = 0xFFD9DFC0;
    } else if (natural == "bare_rock" || natural == "scree" || natural == "shingle") {
        style.fill = natural == "bare_rock" ? 0xFFD4D2CB : 0xFFE1DED4;
        style.layer = 1;
    } else if (natural == "water" || natural == "wetland") {
        style.fill = natural == "water" ? 0xFFA3CEE5 : 0xFFBAD7CB;
        style.layer = 2;
    } else if (natural == "sand" || natural == "beach") {
        style.fill = 0xFFF0E3B5;
    }
    if (tagValue(tags, "leisure") == "park" || tagValue(tags, "leisure") == "garden") {
        style.fill = 0xFFC1DFB4;
    }
    if (tagValue(tags, "amenity") == "parking") {
        style.fill = 0xFFDDD9D1;
    }
    if (!building.empty() && building != "no") {
        style.fill = 0xFFCDBAAE;
        style.layer = 3;
    }
    if (!highway.empty()) {
        style.layer = 5;
        style.stroke = 0xFFFFFAEE;
        style.width = 3.5;
        if (highway == "path" || highway == "footway" || highway == "bridleway" || highway == "steps") {
            style.stroke = 0xFFAD8C6E;
            style.width = 1.0;
        } else if (highway == "track") {
            style.stroke = 0xFFC4AC80;
            style.width = 1.8;
        } else if (highway == "primary" || highway == "secondary" || highway == "trunk" || highway == "motorway") {
            style.stroke = 0xFFF2CB83;
            style.width = 5;
        }
    } else if (!tagValue(tags, "waterway").empty()) {
        style.stroke = 0xFF78B3D2;
        style.width = 1.5;
        style.layer = 4;
    } else if (natural == "cliff") {
        style.stroke = 0xFF9A9183;
        style.width = 1;
    } else if (tags.empty()) {
        style.stroke = 0xFF263E52;
        style.width = 2;
    }
    if (tagValue(tags, "area") == "no") {
        style.fill = 0;
    }
    return style;
}

} // namespace osm
