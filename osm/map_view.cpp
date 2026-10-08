#include "map_view.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osm {

MapView::MapView(const Map::Region& region)
    : m_initial(region), m_region(region),
      m_longitudeScale(std::max(1e-6, std::cos((region.min_lat + region.max_lat) * 0.5 * std::numbers::pi / 180.0)))
{
}

const Map::Region& MapView::getRegion() const
{
    return m_region;
}

Map::Coordinate MapView::coordinateAt(double x, double y) const
{
    return {
        m_region.min_lon + x * (m_region.max_lon - m_region.min_lon),
        m_region.max_lat - y * (m_region.max_lat - m_region.min_lat)
    };
}

bool MapView::setRegion(Map::Region region)
{
    const double width = region.max_lon - region.min_lon;
    const double height = region.max_lat - region.min_lat;

    if (!std::isfinite(region.min_lon) || !std::isfinite(region.max_lon)
        || !std::isfinite(region.min_lat) || !std::isfinite(region.max_lat)
        || width <= 0 || height <= 0 || width > 360 || height > 180)
    {
        return false;
    }

    region.min_lon = std::clamp(region.min_lon, -180.0, 180.0 - width);
    region.max_lon = region.min_lon + width;
    region.min_lat = std::clamp(region.min_lat, -90.0, 90.0 - height);
    region.max_lat = region.min_lat + height;

    if (region.min_lon == m_region.min_lon && region.max_lon == m_region.max_lon
        && region.min_lat == m_region.min_lat && region.max_lat == m_region.max_lat)
    {
        return false;
    }

    m_region = region;

    return true;
}

bool MapView::pan(double dx, double dy)
{
    if (!std::isfinite(dx) || !std::isfinite(dy)) {
        return false;
    }

    const double longitude = -dx * (m_region.max_lon - m_region.min_lon);
    const double latitude = dy * (m_region.max_lat - m_region.min_lat);

    return setRegion({
        m_region.min_lat + latitude,
        m_region.min_lon + longitude,
        m_region.max_lat + latitude,
        m_region.max_lon + longitude
    });
}

bool MapView::zoom(double factor, double x, double y)
{
    if (!std::isfinite(factor) || factor <= 0 || !std::isfinite(x) || !std::isfinite(y)
        || x < 0 || x > 1 || y < 0 || y > 1)
    {
        return false;
    }

    const double width = m_region.max_lon - m_region.min_lon;
    const double height = m_region.max_lat - m_region.min_lat;

    if (width <= 0 || height <= 0) {
        return false;
    }
    // Bound the view by the world and limit magnification to avoid unstable
    // coordinate spans. Near world edges, keeping valid bounds takes priority
    // over preserving the cursor anchor.
    const double minWidth = std::min(width, std::max(1e-9, (m_initial.max_lon - m_initial.min_lon) * 1e-6));
    const double minHeight = std::min(height, std::max(1e-9, (m_initial.max_lat - m_initial.min_lat) * 1e-6));
    const double scale = std::clamp(1.0 / factor,
        std::max(minWidth / width, minHeight / height), std::min(360.0 / width, 180.0 / height));
    const double newWidth = std::min(360.0, width * scale);
    const double newHeight = std::min(180.0, height * scale);
    const auto anchor = coordinateAt(x, y);
    const double left = anchor.longitude - x * newWidth;
    const double bottom = anchor.latitude - (1 - y) * newHeight;

    return setRegion({bottom, left, bottom + newHeight, left + newWidth});
}

bool MapView::reset()
{
    return setRegion(fittedRegion());
}

Map::Region MapView::fittedRegion() const
{
    if (m_width == 0 || m_height == 0) {
        return m_initial;
    }

    double height = std::max(m_initial.max_lat - m_initial.min_lat,
        (m_initial.max_lon - m_initial.min_lon) * m_longitudeScale * m_height / m_width);
    double width = height * m_width / m_height / m_longitudeScale;
    const double limit = std::min({1.0, 360.0 / width, 180.0 / height});
    width *= limit;
    height *= limit;
    const double longitude = (m_initial.min_lon + m_initial.max_lon) / 2;
    const double latitude = (m_initial.min_lat + m_initial.max_lat) / 2;

    return {
        latitude - height / 2,
        longitude - width / 2,
        latitude + height / 2,
        longitude + width / 2
    };
}

bool MapView::resize(int width, int height)
{
    if (width <= 0 || height <= 0 || (width == m_width && height == m_height)) {
        return false;
    }

    const int oldWidth = m_width;
    const int oldHeight = m_height;
    m_width = width;
    m_height = height;

    if (oldWidth == 0 || oldHeight == 0) {
        return setRegion(fittedRegion());
    }
    const auto center = coordinateAt(0.5, 0.5);
    double longitudeSpan = (m_region.max_lon - m_region.min_lon) * width / oldWidth;
    double latitudeSpan = (m_region.max_lat - m_region.min_lat) * height / oldHeight;

    // At world limits, reduce both axes together to preserve the projection.
    const double limit = std::min({1.0, 360.0 / longitudeSpan, 180.0 / latitudeSpan});
    longitudeSpan *= limit;
    latitudeSpan *= limit;

    return setRegion({
        center.latitude - latitudeSpan / 2, center.longitude - longitudeSpan / 2,
        center.latitude + latitudeSpan / 2, center.longitude + longitudeSpan / 2
    });
}

} // namespace osm
