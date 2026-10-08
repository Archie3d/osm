#pragma once

#include <filesystem>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string>
#include <blend2d/blend2d.h>
#include "result.h"
#include "style.h"

namespace osm {

/**
 * @brief Represents a map in the OpenStreetMap format.
 */
class Map
{
public:
    /**
     * @brief Point coordinate on the map.
     */
    struct Coordinate
    {
        double longitude;
        double latitude;
    };

    /**
     * @brief Rectangular region on the map.
     */
    struct Region
    {
        double min_lat{};
        double min_lon{};
        double max_lat{};
        double max_lon{};
    };


    /**
     * @brief Construct a new Map object
     */
    Map();
    virtual ~Map();

    /**
     * @brief The region of the entire map.
     * @return const Region&
     */
    const Region& getRegion() const;

    /**
     * @brief Nonfatal issues, such as incomplete multipolygons in partial extracts.
     */
    const std::vector<std::string>& getWarnings() const;

    /**
     * @brief Loads a map from the specified OpenStreetMap file.
     *
     * @param filePath The path to the OpenStreetMap file to load.
     * @return Result<Map>
     */
    static Result<Map> load(const std::filesystem::path& filePath);

    /**
     * @brief Renders the specified region of the map using the provided Blend2D context.
     *
     * @param ctx Blend2D context used for rendering the map region.
     * @param region The region of the map to render.
     * Invalid or zero-area regions return an error; empty geometry draws nothing.
     * Existing context state and clipping are preserved.
     * @return Result<void>
     */
    Result<void> render(BLContext& ctx, const Region& region);

private:

    struct Node
    {
        double latitude;
        double longitude;
        Tags tags;
    };

    struct LabelMetadata
    {
        std::string m_name;
        int m_priority{};
        float m_size{12};
        uint32_t m_color{0xFF45483E};
    };

    struct Way
    {
        std::vector<std::int64_t> nodes;
        Tags tags;
        bool relationMember{};
        bool areaMember{};
        Style m_style;
        Region m_bounds;
        LabelMetadata m_label;
        bool m_closed{};
        bool m_casing{};
    };

    struct Area
    {
        std::vector<std::vector<std::int64_t>> rings;
        Tags tags;
        Style m_style;
        Region m_bounds;
        LabelMetadata m_label;
    };

    struct Fill
    {
        std::size_t m_index;
        bool m_isArea;
    };

    struct NodeLabel
    {
        std::int64_t m_id;
        LabelMetadata m_label;
    };

    void prepareRenderData();
    static LabelMetadata labelMetadata(const Tags& tags, int priority);

    std::vector<Fill> m_fills;
    std::vector<std::size_t> m_lines;
    std::vector<NodeLabel> m_nodeLabels;
    Region m_region;
    std::unordered_map<std::int64_t, Node> m_nodes;
    std::vector<Way> m_ways;
    std::vector<Area> m_areas;
    std::vector<std::string> m_warnings;

};

} // namespace osm
