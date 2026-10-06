/**
 * @file layout.hpp
 * @brief Diagram layout of a canonical model (format twin-ta-layout/1): presentation only.
 * @ingroup authoring
 *
 * Positions of locations, bend points (nails) of edges and label anchors, keyed
 * by location name and edge id. The layout is stored next to a model version
 * but never hashed into evidence or packages: moving a node never invalidates
 * alignment, compilation or any other formal result.
 */
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"

namespace twin::authoring {

/// @brief Format tag of layout documents.
inline constexpr std::string_view kLayoutFormat = "twin-ta-layout/1";

/// @brief A point in diagram coordinates (UPPAAL editor convention: integers, y downwards).
struct Point {
    std::int64_t x{0};  ///< Horizontal coordinate.
    std::int64_t y{0};  ///< Vertical coordinate.
    /// @brief Member-wise equality.
    friend bool operator==(const Point&, const Point&) = default;
};

/// @brief Layout of a location.
struct LocationLayout {
    Point position;              ///< Centre of the location.
    std::optional<Point> label;  ///< Anchor of the name label, if placed explicitly.
    /// @brief Member-wise equality.
    friend bool operator==(const LocationLayout&, const LocationLayout&) = default;
};

/// @brief Layout of an edge.
struct EdgeLayout {
    std::vector<Point> nails;    ///< Bend points, from source to target.
    std::optional<Point> label;  ///< Anchor of the label block, if placed explicitly.
    /// @brief Member-wise equality.
    friend bool operator==(const EdgeLayout&, const EdgeLayout&) = default;
};

/// @brief Layout of a whole model.
struct Layout {
    std::map<std::string, LocationLayout> locations;  ///< By location name.
    std::map<std::string, EdgeLayout> edges;          ///< By edge id.
    /// @brief Member-wise equality.
    friend bool operator==(const Layout&, const Layout&) = default;
};

/// @brief Encode as twin-ta-layout/1: {format, locations:{name:{x,y,label?}}, edges:{id:{nails,label?}}}.
[[nodiscard]] json::Json to_json(const Layout& layout);
/// @brief Decode a twin-ta-layout/1 document strictly (integer coordinates only).
[[nodiscard]] Result<Layout> layout_from_json(const json::Json& document);

}  // namespace twin::authoring
