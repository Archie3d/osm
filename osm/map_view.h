#pragma once

#include "osm.h"

namespace osm {

// View coordinates are fractions of the displayed map rectangle, independent
// of window size or image resolution. (0, 0) is its north-west corner.
class MapView
{
public:
    explicit MapView(const Map::Region& region);

    const Map::Region& getRegion() const;
    Map::Coordinate coordinateAt(double x, double y) const;
    bool pan(double dx, double dy);
    bool zoom(double factor, double x = 0.5, double y = 0.5);
    bool reset();
    // Fit the initial map on first use. Subsequent resizes retain the center
    // and map scale, expanding/cropping the view rather than stretching it.
    bool resize(int width, int height);

private:
    bool setRegion(Map::Region region);
    Map::Region fittedRegion() const;

    Map::Region m_initial;
    Map::Region m_region;
    double m_longitudeScale;
    int m_width{};
    int m_height{};
};

} // namespace osm
