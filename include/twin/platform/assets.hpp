/**
 * @file assets.hpp
 * @brief Asset registry and operational knowledge graph (asset *instances* and their relationships).
 * @ingroup platform
 *
 * This is the **asset knowledge graph**: concrete things (Plant A, Line 4,
 * Pump P-101, Bearing B4, Sensor TT-101) and operational relationships
 * between them (`feeds`, `powers`, `monitors`, `observedBy`, ...), plus the
 * containment hierarchy (`parent_id`).
 *
 * It is **not** the formal ontology. The formal ontology (twin::ontology) is
 * a first-order theory of domain semantics used by interpretation and
 * alignment. Assets may *reference* formal artefacts (an asset is bound to a
 * twin, whose deployment names an ontology version), but the two are distinct
 * artefacts with distinct lifecycles.
 *
 * Graph queries are neighbourhood-based (bounded depth and node count), so
 * large estates are explored around a focus instead of rendered whole.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/platform/database.hpp"

namespace twin::platform {

/// @brief An asset instance.
struct Asset {
    std::string id;                          ///< Stable id, e.g. "pump-p101".
    std::string name;                        ///< Display name, e.g. "Pump P-101".
    std::string type;                        ///< Asset type, e.g. "Pump", "Line", "Sensor".
    std::optional<std::string> parent_id;    ///< Containing asset (hierarchy).
    std::string description;                 ///< Free text.
    json::Json tags = json::Json::array();   ///< Array of strings.
    json::Json properties = json::Json::object();  ///< Metadata (manufacturer, serial, location, ...).
    std::optional<std::string> twin_id;      ///< Digital twin bound to this asset, if any.
};

/// @brief A typed, directed relationship between two assets.
struct Relationship {
    std::int64_t id{0};          ///< Row id.
    std::string source_id;       ///< From.
    std::string type;            ///< e.g. "feeds", "observedBy".
    std::string target_id;       ///< To.
    json::Json properties = json::Json::object();  ///< Extra attributes.
};

/// @brief Filter for listing assets.
struct AssetFilter {
    std::optional<std::string> type;       ///< Exact type.
    std::optional<std::string> parent_id;  ///< Children of this asset ("" = roots).
    std::optional<std::string> text;       ///< Case-insensitive substring of id/name/type/description.
    std::int64_t limit{200};               ///< Page size.
    std::int64_t offset{0};                ///< Page offset.
};

/// @brief A bounded neighbourhood of the graph.
struct Neighborhood {
    std::vector<Asset> nodes;                 ///< Assets within the bound (focus first).
    std::vector<Relationship> edges;          ///< Edges among them; hierarchy edges have type "contains" and id 0.
    std::vector<std::string> frontier;        ///< Nodes with unexplored neighbours (expandable).
    bool truncated{false};                    ///< max_nodes was reached.
};

/// @brief Asset registry (see file documentation).
class AssetRepository {
public:
    /// @brief Repository over @p db.
    explicit AssetRepository(Database& db) : db_(db) {}

    /// @brief Insert or replace an asset.
    [[nodiscard]] Status upsert(const Asset& asset);
    /// @brief One asset.
    [[nodiscard]] Result<Asset> get(std::string_view id) const;
    /// @brief Assets matching @p filter, ordered by name.
    [[nodiscard]] Result<std::vector<Asset>> list(const AssetFilter& filter) const;
    /// @brief Total count for @p filter.
    [[nodiscard]] Result<std::int64_t> count(const AssetFilter& filter) const;
    /// @brief The chain root … parent of @p id (for breadcrumbs), root first.
    [[nodiscard]] Result<std::vector<Asset>> ancestors(std::string_view id) const;
    /// @brief Number of direct children of @p id.
    [[nodiscard]] Result<std::int64_t> child_count(std::string_view id) const;

    /// @brief Add a relationship (idempotent on source/type/target).
    [[nodiscard]] Result<Relationship> relate(std::string_view source, std::string_view type, std::string_view target,
                                              const json::Json& properties = json::Json::object());
    /// @brief Relationships where @p id is source or target.
    [[nodiscard]] Result<std::vector<Relationship>> relationships_of(std::string_view id) const;
    /// @brief Distinct relationship types with counts.
    [[nodiscard]] Result<std::vector<std::pair<std::string, std::int64_t>>> relationship_types() const;
    /// @brief Distinct asset types with counts.
    [[nodiscard]] Result<std::vector<std::pair<std::string, std::int64_t>>> asset_types() const;

    /**
     * @brief Breadth-first neighbourhood of @p focus.
     * @param focus Asset the neighbourhood is centred on.
     * @param depth Maximum hops (relationships and hierarchy in both directions).
     * @param relationship_types If non-empty, only these types ("contains" = hierarchy).
     * @param max_nodes Hard bound on returned nodes.
     */
    [[nodiscard]] Result<Neighborhood> neighborhood(std::string_view focus, int depth,
                                                    const std::vector<std::string>& relationship_types,
                                                    std::size_t max_nodes) const;

private:
    Database& db_;
};

/// @brief API representation.
[[nodiscard]] json::Json to_json(const Asset& asset);
/// @brief API representation.
[[nodiscard]] json::Json to_json(const Relationship& relationship);

}  // namespace twin::platform
