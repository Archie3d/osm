#include "osm.h"
#include "labels.h"
#include "map_view.h"
#include <cmath>
#include <numbers>
#include <algorithm>

#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void check(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TemporaryFile
{
public:
    TemporaryFile()
    {
        m_path = std::filesystem::temp_directory_path()
            / ("osm-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".osm");
    }

    ~TemporaryFile()
    {
        std::error_code error;
        std::filesystem::remove(m_path, error);
    }

    osm::Result<osm::Map> load(const std::string& xml)
    {
        std::ofstream stream(m_path);
        stream << xml;
        stream.close();
        return osm::Map::load(m_path);
    }

private:
    std::filesystem::path m_path;
};

void testLoader()
{
    auto fixture = osm::Map::load(OSM_FIXTURE);
    check(fixture.has_value(), "Fixture must load");
    check(fixture->getRegion().min_lat == 0 && fixture->getRegion().max_lon == 10, "Explicit bounds");
    check(!osm::Map::load(std::filesystem::path(OSM_FIXTURE) / "missing"), "Missing file");
    check(!osm::Map::load(std::filesystem::path(OSM_FIXTURE).parent_path()), "Unreadable input (directory)");

    TemporaryFile file;
    auto derived = file.load("<osm><way id='1'><nd ref='-1'/><nd ref='2'/></way><node id='-1' lat='3' lon='4'/><node id='2' lat='-2' lon='7'/></osm>");
    check(derived.has_value(), "Forward references and negative IDs");
    const auto& region = derived->getRegion();
    check(region.min_lat == -2 && region.max_lat == 3 && region.min_lon == 4 && region.max_lon == 7, "Node-derived bounds");
    check(file.load("<osm><node id='1' lat='0' lon='0'/></osm>").has_value(), "Single node loads");

    for (const std::string xml : {
        "", "<osm>", "<other/>", "<osm/><osm/>", "<osm/>",
        "<osm><bounds minlat='0' minlon='0' maxlat='1' maxlon='1'/></osm>",
        "<osm><node id='1' lat='nan' lon='0'/></osm>",
        "<osm><node id='1' lat='inf' lon='0'/></osm>",
        "<osm><node id='9223372036854775808' lat='0' lon='0'/></osm>",
        "<osm><node id='1' lat='0' lon='0'/><way id='0'><nd ref='1'/><nd ref='1'/></way></osm>",
        "<osm><node id='1' lat='0' lon='0'/><way id='1'><nd ref='1'/><nd ref='1'/></way><way id='1'><nd ref='1'/><nd ref='1'/></way></osm>",
        "<osm><bounds minlat='nan' minlon='0' maxlat='1' maxlon='1'/><node id='1' lat='0' lon='0'/></osm>",
        "<osm><bounds minlat='0' minlon='0' maxlat='1' maxlon='1'/><bounds minlat='0' minlon='0' maxlat='1' maxlon='1'/><node id='1' lat='0' lon='0'/></osm>",
        "<osm><node id='1' lat='91' lon='0'/></osm>",
        "<osm><node id='1' lat='0' lon='181'/></osm>",
        "<osm><node id='1' lat='1x' lon='0'/></osm>",
        "<osm><node id='1' lon='0'/></osm>",
        "<osm><node id='0' lat='0' lon='0'/></osm>",
        "<osm><node id='1' lat='0' lon='0'/><node id='1' lat='1' lon='1'/></osm>",
        "<osm><bounds minlat='2' minlon='0' maxlat='1' maxlon='1'/><node id='1' lat='0' lon='0'/></osm>",
        "<osm><bounds/><node id='1' lat='0' lon='0'/></osm>",
        "<osm><node id='1' lat='0' lon='0'/><way id='1'><nd ref='1'/><nd ref='2'/></way></osm>",
        "<osm><node id='1' lat='0' lon='0'/><way id='1'><nd ref='x'/><nd ref='1'/></way></osm>",
        "<osm><node id='1' lat='0' lon='0'/><way id='1'><nd ref='1'/></way></osm>"
    }) {
        auto result = file.load(xml);
        check(!result && !result.error().empty(), "Expected useful error for: " + xml);
    }
}

void testRenderer()
{
    auto map = osm::Map::load(OSM_FIXTURE);
    check(map.has_value(), "Renderer fixture");
    BLImage image(100, 100, BL_FORMAT_PRGB32);
    BLContext context(image);
    auto clear = [&]() {
        check(context.clear_all() == BL_SUCCESS, "Clear image");
    };
    auto pixel = [&](int x, int y) {
        BLImageData data;
        check(image.get_data(&data) == BL_SUCCESS, "Image data");
        return reinterpret_cast<const std::uint32_t*>(static_cast<const unsigned char*>(data.pixel_data) + y * data.stride)[x];
    };
    auto blank = [&]() {
        for (int y = 0; y < 100; ++y) {
            for (int x = 0; x < 100; ++x) {
                if (pixel(x, y) != 0) {
                    return false;
                }
            }
        }
        return true;
    };
    clear();
    context.set_stroke_width(7);
    check(map->render(context, map->getRegion()).has_value(), "Render fixture");
    check(pixel(50, 20) != 0 && pixel(80, 50) != 0, "Visible connected way");
    check(pixel(50, 80) == 0 && pixel(20, 50) == 0, "North-up orientation");
    check(context.stroke_width() == 7, "Restore caller context state");

    clear();
    check(map->render(context, {0, 0, 1, 1}).has_value() && blank(), "Non-intersecting region");
    clear();
    check(map->render(context, {7, 4, 9, 6}).has_value(), "Crossing segment with both endpoints outside");
    check(pixel(0, 50) != 0 && pixel(99, 50) != 0 && pixel(50, 20) == 0, "Clipped segment spans viewport");
    clear();
    context.clip_to_rect(25, 25, 50, 50);
    check(map->render(context, {7, 4, 9, 6}).has_value(), "Honor caller clip");
    check(pixel(50, 50) != 0 && pixel(10, 50) == 0 && pixel(90, 50) == 0, "No pixels outside clip");
    context.restore_clipping();
    check(!map->render(context, {0, 0, 0, 1}), "Zero latitude span");
    check(!map->render(context, {0, 0, 1, 0}), "Zero longitude span");
    check(!map->render(context, {2, 0, 1, 1}), "Reversed region");
    check(!map->render(context, {0, 0, std::numeric_limits<double>::infinity(), 1}), "Nonfinite region");
    BLContext inactive;
    check(!map->render(inactive, map->getRegion()), "Inactive context");
    clear();
    osm::Map empty;
    check(empty.render(context, {0, 0, 1, 1}).has_value() && blank(), "Empty geometry is a no-op");
    context.end();
}

void testAreas()
{
    const std::string nodes =
        "<node id='1' lat='1' lon='1'/><node id='2' lat='1' lon='9'/>"
        "<node id='3' lat='9' lon='9'/><node id='4' lat='9' lon='1'/>"
        "<node id='5' lat='3' lon='3'/><node id='6' lat='3' lon='7'/>"
        "<node id='7' lat='7' lon='7'/><node id='8' lat='7' lon='3'/>"
        "<node id='9' lat='5' lon='0'/><node id='10' lat='5' lon='10'/>";
    const std::string outer = "<nd ref='1'/><nd ref='2'/><nd ref='3'/><nd ref='4'/><nd ref='1'/>";
    const std::string inner = "<nd ref='5'/><nd ref='6'/><nd ref='7'/><nd ref='8'/><nd ref='5'/>";
    TemporaryFile file;
    BLImage image(100, 100, BL_FORMAT_PRGB32);
    BLContext context(image);
    auto load = [&](const std::string& features) {
        auto map = file.load("<osm>" + nodes + features + "</osm>");
        check(map.has_value(), "Area fixture must load");
        return *map;
    };
    auto draw = [&](osm::Map& map, osm::Map::Region region = {0, 0, 10, 10}) {
        check(context.clear_all() == BL_SUCCESS, "Clear area image");
        check(map.render(context, region).has_value(), "Render area fixture");
    };
    auto pixel = [&](int x, int y) {
        BLImageData data;
        check(image.get_data(&data) == BL_SUCCESS, "Area image data");
        return reinterpret_cast<const std::uint32_t*>(static_cast<const unsigned char*>(data.pixel_data) + y * data.stride)[x];
    };
    auto water = load("<way id='1'>" + outer + "<tag k='natural' v='water'/></way>");
    draw(water);
    check(pixel(50, 50) == 0xFFA3CEE5 && pixel(5, 5) == 0, "Closed water fills only its interior");
    draw(water, {4, 4, 6, 6});
    check(pixel(0, 0) == 0xFFA3CEE5 && pixel(99, 99) == 0xFFA3CEE5, "Polygon encloses viewport with every edge outside");
    draw(water, {0, 0, 2, 2});
    check(pixel(75, 25) == 0xFFA3CEE5 && pixel(25, 75) == 0, "Area clips at viewport edges");
    for (const std::string tags : {"", "<tag k='natural' v='water'/><tag k='area' v='no'/>"}) {
        auto outline = load("<way id='1'>" + outer + tags + "</way>");
        draw(outline);
        check(pixel(50, 50) == 0, "Closed ways are not automatically areas");
    }
    auto open = load("<way id='1'><nd ref='1'/><nd ref='2'/><nd ref='3'/><tag k='natural' v='water'/></way>");
    draw(open);
    check(pixel(60, 60) == 0 && pixel(50, 90) != 0, "Open water way stays a line");

    const std::string pieces =
        "<way id='1'><nd ref='1'/><nd ref='2'/><nd ref='3'/></way>"
        "<way id='2'><nd ref='1'/><nd ref='4'/></way>"
        "<way id='3'><nd ref='4'/><nd ref='3'/></way>"
        "<way id='4'>" + inner + "</way>";
    const std::string relation =
        "<relation id='10'><member type='way' ref='3' role='outer'/>"
        "<member type='way' ref='1' role='outer'/><member type='way' ref='2' role='outer'/>"
        "<member type='way' ref='4' role='inner'/><tag k='type' v='multipolygon'/>"
        "<tag k='natural' v='wood'/></relation>";
    auto woodland = load(relation + pieces);
    check(woodland.getWarnings().empty(), "Unordered and reversed members form complete rings");
    draw(woodland);
    check(pixel(20, 50) == 0xFFA8C99A && pixel(50, 50) == 0 && pixel(5, 5) == 0, "Multipolygon preserves inner hole");
    draw(woodland, {4, 4, 6, 6});
    check(pixel(50, 50) == 0, "Viewport wholly inside a hole stays empty");
    auto nested = load("<way id='5'>" + inner + "<tag k='landuse' v='meadow'/></way>"
        + pieces + "<relation id='11'><member type='way' ref='1' role='outer'/>"
        "<member type='way' ref='2' role='outer'/><member type='way' ref='3' role='outer'/>"
        "<tag k='type' v='multipolygon'/><tag k='natural' v='wood'/></relation>");
    draw(nested);
    check(pixel(50, 50) == 0xFFCDE2B5 && pixel(20, 50) == 0xFFA8C99A, "Smaller terrain area remains visible inside a larger relation");
    auto incomplete = load(pieces + "<relation id='20'><member type='way' ref='1' role='outer'/>"
        "<member type='way' ref='999' role='outer'/><tag k='type' v='multipolygon'/>"
        "<tag k='natural' v='wood'/></relation>");
    draw(incomplete);
    check(incomplete.getWarnings().size() == 1 && pixel(20, 50) == 0, "Incomplete relations warn and do not invent areas");
    auto unclosed = load(pieces + "<relation id='20'><member type='way' ref='1' role='outer'/>"
        "<tag k='type' v='multipolygon'/><tag k='natural' v='wood'/></relation>");
    check(unclosed.getWarnings().size() == 1, "Unclosed relation ring warns");

    auto layered = load("<way id='8'><nd ref='9'/><nd ref='10'/><tag k='highway' v='residential'/></way>"
        "<way id='2'>" + inner + "<tag k='building' v='yes'/></way>"
        "<way id='1'>" + outer + "<tag k='landuse' v='meadow'/></way>");
    draw(layered);
    check(pixel(20, 20) == 0xFFCDE2B5 && pixel(50, 40) == 0xFFCDBAAE, "Buildings appear over landuse regardless of XML order");
    check(pixel(50, 50) == 0xFFFFFAEE, "Roads appear over filled regions");
    auto tiedFills = load("<way id='1'>" + outer + "<tag k='landuse' v='meadow'/></way>"
        "<way id='2'>" + outer + "<tag k='natural' v='wood'/></way>");
    draw(tiedFills);
    check(pixel(50, 50) == 0xFFA8C99A, "Equal-layer equal-area fills retain way order");
    auto tiedRelation = load("<way id='1'>" + outer + "<tag k='landuse' v='meadow'/></way>"
        "<way id='2'>" + outer + "</way>"
        "<relation id='3'><member type='way' ref='2' role='outer'/>"
        "<tag k='type' v='multipolygon'/><tag k='natural' v='wood'/></relation>");
    draw(tiedRelation);
    check(pixel(50, 50) == 0xFFA8C99A, "Equal-key relation fills follow way fills");
    auto tiedLines = load("<way id='1'><nd ref='9'/><nd ref='10'/><tag k='highway' v='primary'/></way>"
        "<way id='2'><nd ref='9'/><nd ref='10'/><tag k='highway' v='residential'/></way>");
    draw(tiedLines);
    check(pixel(50, 50) == 0xFFFFFAEE, "Equal-layer strokes retain way order");
    auto copied = woodland;
    woodland = osm::Map{};
    draw(copied);
    check(pixel(20, 50) == 0xFFA8C99A && pixel(50, 50) == 0, "Copied area caches survive source replacement");
    osm::Map assigned;
    assigned = tiedLines;
    tiedLines = osm::Map{};
    draw(assigned);
    check(pixel(50, 50) == 0xFFFFFAEE, "Assigned stroke caches survive source replacement");
    draw(copied, {20, 20, 30, 30});
    check(pixel(50, 50) == 0, "Disjoint cached area bounds reject geometry");
    context.end();
}

void testLabels()
{
    BLImage image(300, 120, BL_FORMAT_PRGB32);
    BLContext context(image);
    auto pixels = [&]() {
        check(context.flush(BL_CONTEXT_FLUSH_SYNC) == BL_SUCCESS, "Flush labels");
        BLImageData data;
        check(image.get_data(&data) == BL_SUCCESS, "Label pixels");
        std::vector<uint32_t> result;
        for (int y = 0; y < 120; ++y) {
            const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(data.pixel_data) + y * data.stride);
            result.insert(result.end(), row, row + 300);
        }
        return result;
    };
    std::vector<osm::Label> candidates = {
        {"Important", {90, 40}, 0, 14},
        {"Conflicting", {90, 40}, 3, 12},
        {"Important", {230, 90}, 0, 14},
        {"Café", {225, 40}, 2, 12},
        {"Clipped at edge", {0, 80}, 0, 12},
        {"Outside", {-100, -100}, 0, 12}
    };
    context.clear_all();
    context.set_stroke_width(9);
    auto placed = osm::renderLabels(context, candidates);
    check(placed && *placed == 2, "Collision, duplicate name, and edge suppression");
    check(context.stroke_width() == 9, "Label renderer restores context state");
    const auto first = pixels();
    check(std::count_if(first.begin(), first.end(), [](uint32_t pixel) {
        return (pixel >> 24) > 200 && ((pixel >> 16) & 255) < 170
            && ((pixel >> 8) & 255) < 170 && (pixel & 255) < 170;
    }) > 10, "Visible text glyphs");
    check(std::count(first.begin(), first.end(), 0xFFF9F7EE) > 10, "Visible pale halo");
    std::reverse(candidates.begin(), candidates.end());
    context.clear_all();
    check(osm::renderLabels(context, candidates).has_value(), "Reordered labels");
    check(pixels() == first, "Placement independent of input order");
    context.clear_all();
    context.clip_to_rect(0, 0, 150, 120);
    check(osm::renderLabels(context, {{"Test", {150, 60}, 0, 14}}).has_value(), "Clipped labels");
    const auto clipped = pixels();
    for (int y = 0; y < 120; ++y) {
        for (int x = 150; x < 300; ++x) {
            check(clipped[y * 300 + x] == 0, "Label pixels honor caller clip");
        }
    }
    context.restore_clipping();

    TemporaryFile file;
    auto renderMap = [&](const std::string& features) {
        auto map = file.load("<osm><bounds minlat='0' minlon='0' maxlat='10' maxlon='10'/>" + features + "</osm>");
        check(map.has_value(), "Named map loads");
        context.clear_all();
        check(map->render(context, map->getRegion()).has_value(), "Named map renders");
        const auto result = pixels();
        auto copied = *map;
        *map = osm::Map{};
        context.clear_all();
        check(copied.render(context, copied.getRegion()).has_value(), "Copied named map renders");
        check(pixels() == result, "Copied label metadata survives source replacement");
        std::string unnamed = features;
        for (auto start = unnamed.find("<tag k='name'"); start != std::string::npos; start = unnamed.find("<tag k='name'")) {
            unnamed.erase(start, unnamed.find("/>", start) + 2 - start);
        }
        auto baseline = file.load("<osm><bounds minlat='0' minlon='0' maxlat='10' maxlon='10'/>" + unnamed + "</osm>");
        check(baseline.has_value(), "Unnamed baseline loads");
        context.clear_all();
        check(baseline->render(context, baseline->getRegion()).has_value(), "Unnamed baseline renders");
        const auto plain = pixels();
        std::size_t changed = 0;
        for (std::size_t i = 0; i < result.size(); ++i) {
            changed += result[i] != plain[i];
        }
        check(changed > 10, "OSM name adds visible text to the map");
    };
    renderMap("<node id='1' lat='5' lon='5'><tag k='name' v='High Hill'/><tag k='natural' v='peak'/></node>");
    renderMap("<node id='1' lat='5' lon='-20'/><node id='2' lat='5' lon='11'/>"
        "<way id='1'><nd ref='1'/><nd ref='2'/><tag k='name' v='Lane'/><tag k='highway' v='track'/></way>");
    const std::string nodes = "<node id='1' lat='1' lon='1'/><node id='2' lat='1' lon='9'/>"
        "<node id='3' lat='9' lon='9'/><node id='4' lat='9' lon='1'/>";
    renderMap(nodes + "<way id='1'><nd ref='1'/><nd ref='2'/><nd ref='3'/><nd ref='4'/><nd ref='1'/>"
        "<tag k='name' v='Wood'/><tag k='natural' v='wood'/></way>");
    renderMap(nodes + "<way id='1'><nd ref='1'/><nd ref='2'/><nd ref='3'/><nd ref='4'/><nd ref='1'/></way>"
        "<relation id='1'><member type='way' ref='1' role='outer'/><tag k='type' v='multipolygon'/>"
        "<tag k='natural' v='heath'/><tag k='name' v='Heath'/></relation>");
    context.end();
}

void testMapView()
{
    const osm::Map::Region initial{50, -4, 54, 4};
    osm::MapView view(initial);
    auto near = [](double actual, double expected) {
        check(std::abs(actual - expected) < 1e-9, "Coordinate mismatch");
    };
    auto topLeft = view.coordinateAt(0, 0);
    near(topLeft.longitude, -4);
    near(topLeft.latitude, 54);
    auto bottomRight = view.coordinateAt(1, 1);
    near(bottomRight.longitude, 4);
    near(bottomRight.latitude, 50);
    const auto anchor = view.coordinateAt(0.25, 0.75);
    check(view.zoom(2, 0.25, 0.75), "Zoom changes view");
    near(view.getRegion().max_lon - view.getRegion().min_lon, 4);
    near(view.getRegion().max_lat - view.getRegion().min_lat, 2);
    auto zoomedAnchor = view.coordinateAt(0.25, 0.75);
    near(zoomedAnchor.longitude, anchor.longitude);
    near(zoomedAnchor.latitude, anchor.latitude);
    check(view.pan(0.1, 0.2), "Pan changes view");
    const auto draggedAnchor = view.coordinateAt(0.35, 0.95);
    near(draggedAnchor.longitude, anchor.longitude);
    near(draggedAnchor.latitude, anchor.latitude);
    // Display size changes do not change geographic coordinates at the same
    // image fraction (e.g. 200/800 pixels and 400/1600 pixels).
    near(view.coordinateAt(200.0 / 800, 150.0 / 600).longitude,
        view.coordinateAt(400.0 / 1600, 300.0 / 1200).longitude);
    check(view.reset(), "Reset changes view");
    near(view.getRegion().min_lon, initial.min_lon);
    near(view.getRegion().max_lat, initial.max_lat);
    check(!view.reset(), "Reset is idempotent");
    view.zoom(3, 0.1, 0.9);
    view.zoom(1.0 / 3, 0.1, 0.9);
    near(view.getRegion().min_lon, initial.min_lon);
    near(view.getRegion().max_lat, initial.max_lat);
    check(!view.zoom(0) && !view.zoom(-1) && !view.zoom(2, -1, 0), "Invalid zoom is ignored");
    check(!view.pan(std::numeric_limits<double>::infinity(), 0), "Nonfinite pan is ignored");
    check(!view.zoom(std::numeric_limits<double>::quiet_NaN()), "Nonfinite zoom is ignored");
    view.pan(1000, 1000);
    near(view.getRegion().min_lon, -180);
    near(view.getRegion().max_lat, 90);
    near(view.getRegion().max_lon - view.getRegion().min_lon, 8);
    near(view.getRegion().max_lat - view.getRegion().min_lat, 4);
    view.zoom(1e-20);
    check(view.getRegion().min_lon >= -180 && view.getRegion().max_lon <= 180
        && view.getRegion().min_lat >= -90 && view.getRegion().max_lat <= 90, "Zoom out stays inside world");
    for (int i = 0; i < 100; ++i) {
        view.zoom(10);
    }
    check(view.getRegion().max_lon > view.getRegion().min_lon
        && view.getRegion().max_lat > view.getRegion().min_lat, "Zoom limit retains nonzero bounds");
    check(view.getRegion().max_lon - view.getRegion().min_lon >= 8e-6 - 1e-12, "Maximum magnification is bounded");
    view.reset();
    // Renderer and mouse coordinate lookup agree after combined navigation.
    auto fixture = osm::Map::load(OSM_FIXTURE);
    check(fixture.has_value(), "Navigation render fixture");
    osm::MapView mapView(fixture->getRegion());
    mapView.zoom(2);
    mapView.pan(-0.1, 0.2);
    BLImage image(100, 100, BL_FORMAT_PRGB32);
    BLContext context(image);
    context.clear_all();
    check(fixture->render(context, mapView.getRegion()).has_value(), "Navigated map renders");
    BLImageData data;
    check(image.get_data(&data) == BL_SUCCESS, "Navigated pixels");
    // The fixture's horizontal way is at latitude 8; the new view is [3.5, 8.5].
    const auto position = mapView.coordinateAt(0.5, 0.1);
    near(position.latitude, 8);
    const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(data.pixel_data) + 10 * data.stride);
    check(row[50] != 0, "Rendered way matches coordinates displayed under mouse");
    context.end();
}

void testMapResize()
{
    const osm::Map::Region initial{50, -4, 54, 4};
    const double longitudeScale = std::cos(52 * std::numbers::pi / 180);
    osm::MapView view(initial);
    auto near = [](double a, double b) {
        check(std::abs(a - b) < 1e-9, "Resize coordinate mismatch");
    };
    auto aspect = [&](int width, int height) {
        const auto& region = view.getRegion();
        near((region.max_lon - region.min_lon) * longitudeScale / width,
            (region.max_lat - region.min_lat) / height);
    };
    check(view.resize(1000, 700), "Initial window fit");
    aspect(1000, 700);
    check(view.getRegion().min_lon <= initial.min_lon && view.getRegion().max_lon >= initial.max_lon
        && view.getRegion().min_lat <= initial.min_lat && view.getRegion().max_lat >= initial.max_lat,
        "Initial fit contains the entire map");
    view.zoom(2);
    view.pan(0.1, -0.1);
    const auto center = view.coordinateAt(0.5, 0.5);
    const double scale = (view.getRegion().max_lat - view.getRegion().min_lat) / 700;
    for (const auto size : {std::pair{500, 900}, std::pair{1400, 350}, std::pair{700, 700}, std::pair{1000, 700}}) {
        check(view.resize(size.first, size.second), "Resize changes canvas");
        aspect(size.first, size.second);
        near(view.coordinateAt(0.5, 0.5).longitude, center.longitude);
        near(view.coordinateAt(0.5, 0.5).latitude, center.latitude);
        near((view.getRegion().max_lat - view.getRegion().min_lat) / size.second, scale);
        const auto offset = view.coordinateAt(0.5 + 40.0 / size.first, 0.5 + 30.0 / size.second);
        near(offset.longitude - center.longitude, 40 * scale / longitudeScale);
        near(center.latitude - offset.latitude, 30 * scale);
    }
    check(!view.resize(1000, 700) && !view.resize(0, 700) && !view.resize(700, -1), "Unchanged or invalid resize is ignored");
    view.resize(400, 900);
    view.reset();
    aspect(400, 900);
    near(view.coordinateAt(0.5, 0.5).longitude, 0);
    near(view.coordinateAt(0.5, 0.5).latitude, 52);
    check(view.getRegion().min_lon <= initial.min_lon && view.getRegion().max_lon >= initial.max_lon,
        "Reset refits map to portrait window");
    const auto anchor = view.coordinateAt(0.3, 0.6);
    view.zoom(2, 0.3, 0.6);
    near(view.coordinateAt(0.3, 0.6).longitude, anchor.longitude);
    near(view.coordinateAt(0.3, 0.6).latitude, anchor.latitude);
    view.pan(0.1, 0.1);
    near(view.coordinateAt(0.4, 0.7).longitude, anchor.longitude);
    near(view.coordinateAt(0.4, 0.7).latitude, anchor.latitude);
    view.resize(20000, 20000);
    aspect(20000, 20000);
    check(view.getRegion().min_lon >= -180 && view.getRegion().max_lon <= 180
        && view.getRegion().min_lat >= -90 && view.getRegion().max_lat <= 90, "Large resize respects world bounds");
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        check(argc == 2, "Specify loader or renderer");
        if (std::string(argv[1]) == "loader") {
            testLoader();
        } else if (std::string(argv[1]) == "renderer") {
            testRenderer();
        } else if (std::string(argv[1]) == "areas") {
            testAreas();
        } else if (std::string(argv[1]) == "labels") {
            testLabels();
        } else if (std::string(argv[1]) == "view") {
            testMapView();
        } else if (std::string(argv[1]) == "resize") {
            testMapResize();
        } else {
            throw std::runtime_error("Unknown test group");
        }
        std::cout << argv[1] << " tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
