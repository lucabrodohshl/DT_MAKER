/**
 * @file world.hpp
 * @brief The generic World & Layout document of a Twin Blueprint (format twin-world/1).
 * @ingroup scene
 *
 * @defgroup scene World & layout (scene model)
 * @brief Physical, spatial and topological context of a twin — deliberately OUTSIDE the
 *        trusted semantic kernel and independent of any domain.
 *
 * A world is a set of **layers** holding **objects**. The schema knows only geometry and
 * roles; it hard-codes no domain vocabulary (no "wall", "room" or "drone"). Domain meaning
 * is carried by an object's free-text `semanticType`, supplied by template tool palettes
 * and interpreted by adapters such as the mobile-robot simulation adapter (robot_sim.hpp).
 *
 * @code
 * {"format": "twin-world/1", "mode": "spatial|topology|diagram", "unit": "mm|px",
 *  "bounds": {"x","y","w","h"}, "grid": {"size", "snap"},
 *  "layers":  [{"id","name","role","visible","locked"}],
 *  "objects": [{"id","layer","kind","semanticType","name","geometry":{...},
 *               "properties":{...},"asset":"<asset id>","tags":[...],"style":{...}}]}
 * @endcode
 *
 * Layer roles separate what physically exists from what the twin is told (see LayerRole):
 * the simulator's ground truth is composed from `shared` and `ground-truth` layers, the
 * twin's initial knowledge from `shared` and `knowledge` layers. `event` layers hold
 * geometry applied only by timeline events, `annotation` layers presentation only and
 * `background` layers imported images, which never become semantic geometry.
 *
 * Coordinates are integers (millimetres for spatial worlds, abstract units otherwise), so
 * the document is canonical JSON and its hash is stable.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"

namespace twin::scene {

/// @brief Format tag of world documents.
inline constexpr std::string_view kWorldFormat = "twin-world/1";

/// @brief What a layer contributes to (see file documentation).
enum class LayerRole {
    Shared,       ///< Both the physical ground truth and the twin's initial knowledge.
    GroundTruth,  ///< Physical / simulation ground truth only (hidden from the twin).
    Knowledge,    ///< The twin's initial knowledge only (e.g. an outdated plan, unknown areas).
    Event,        ///< Geometry applied only by timeline or scenario events.
    Annotation,   ///< Presentation only (labels, zones, markers).
    Background    ///< Imported images; never semantic geometry.
};

/// @brief "shared", "ground-truth", "knowledge", "event", "annotation" or "background".
[[nodiscard]] std::string_view to_string(LayerRole role) noexcept;
/// @brief Inverse of to_string(LayerRole).
[[nodiscard]] std::optional<LayerRole> layer_role_from_string(std::string_view text) noexcept;

/// @brief An integer point in world units.
struct Vec {
    std::int64_t x{0};  ///< Horizontal coordinate (east / right).
    std::int64_t y{0};  ///< Vertical coordinate (south / down).
    /// @brief Member-wise equality.
    friend bool operator==(const Vec&, const Vec&) = default;
};

/// @brief An axis-aligned rectangle in world units.
struct Rect {
    std::int64_t x{0};  ///< Left.
    std::int64_t y{0};  ///< Top.
    std::int64_t w{0};  ///< Width (> 0).
    std::int64_t h{0};  ///< Height (> 0).
    /// @brief Member-wise equality.
    friend bool operator==(const Rect&, const Rect&) = default;
};

/// @brief Geometry of an object; which members apply depends on the object's kind.
struct Geometry {
    std::vector<Vec> points;     ///< point/label/waypoint: 1; line: 2; polyline: >= 2; polygon/region/zone: >= 3.
    std::optional<Rect> rect;    ///< rect, image and (optionally) node.
    std::int64_t width{0};       ///< Stroke width of line/polyline (world units, >= 0).
    std::string from;            ///< edge/connector: source object id.
    std::string to;              ///< edge/connector: target object id.
    bool directed{false};        ///< edge/connector: directed?
};

/// @brief A world object.
struct Object {
    std::string id;             ///< Unique id.
    std::string layer;          ///< Layer id.
    std::string kind;           ///< Geometric kind (see kKinds).
    std::string semantic_type;  ///< Domain vocabulary ("wall", "tank", ...); may be empty.
    std::string name;           ///< Display name.
    Geometry geometry;          ///< Geometry.
    json::Json properties = json::Json::object();  ///< Domain properties (strings, integers, booleans).
    std::optional<std::string> asset;  ///< Bound asset id (structure model), if any.
    std::vector<std::string> tags;     ///< Semantic tags.
};

/// @brief A layer.
struct Layer {
    std::string id;                   ///< Unique id.
    std::string name;                 ///< Display name.
    LayerRole role{LayerRole::Shared};  ///< Contribution (see LayerRole).
    bool visible{true};               ///< Shown in editors by default.
    bool locked{false};               ///< Protected from editing in editors.
};

/// @brief A decoded world document.
struct World {
    std::string mode{"spatial"};   ///< "spatial", "topology" or "diagram".
    std::string unit{"mm"};        ///< "mm" (spatial) or "px".
    Rect bounds;                   ///< Extent of the world.
    std::vector<Layer> layers;     ///< Layers, bottom to top.
    std::vector<Object> objects;   ///< Objects, drawing order (later on top).

    /// @brief The layer @p id, or nullptr.
    [[nodiscard]] const Layer* layer(std::string_view id) const noexcept;
    /// @brief The object @p id, or nullptr.
    [[nodiscard]] const Object* object(std::string_view id) const noexcept;
};

/// @brief Geometric object kinds understood by the schema.
inline constexpr std::string_view kKinds[] = {"point", "label", "waypoint", "line", "polyline", "polygon", "region",
                                              "zone",  "rect",  "image",    "node", "edge",     "connector"};

/// @brief A structural finding about a world document.
struct Finding {
    std::string severity;  ///< "error" or "warning".
    std::string code;      ///< TWW0xx.
    std::string message;   ///< Actionable explanation.
    std::string object;    ///< Object or layer id it concerns ("" for the document).
    std::string path;      ///< JSON path, e.g. "objects[3].geometry".
};

/// @brief An empty spatial world of the given extent with one shared layer.
[[nodiscard]] json::Json empty_world(std::int64_t width_mm, std::int64_t height_mm);

/**
 * @brief Decode a twin-world/1 document.
 *
 * Refuses (ValidationError naming the path) a wrong format tag, floating-point numbers,
 * malformed members and kinds outside kKinds. Reference and geometry rules are reported by
 * validate(), so an editor can still load a world that has findings.
 */
[[nodiscard]] Result<World> world_from_json(const json::Json& document);

/// @brief All structural findings: TWW001 duplicate id, TWW002 unknown layer, TWW003 bad
/// geometry, TWW004 unknown edge endpoint, TWW005 bad bounds, TWW007 object outside the bounds,
/// TWW008 image outside a background layer.
[[nodiscard]] std::vector<Finding> validate(const World& world);

/// @brief API form of findings.
[[nodiscard]] json::Json to_json(const std::vector<Finding>& findings);

/// @brief True iff any finding is an error.
[[nodiscard]] bool has_errors(const std::vector<Finding>& findings) noexcept;

/// @brief Axis-aligned bounding box of an object's geometry (std::nullopt for edges).
[[nodiscard]] std::optional<Rect> bounding_box(const Object& object);

}  // namespace twin::scene
