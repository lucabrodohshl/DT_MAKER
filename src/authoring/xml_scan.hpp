/**
 * @file xml_scan.hpp
 * @brief Internal: a minimal, non-validating XML reader for the presentational parts of UPPAAL files.
 *
 * Used only for what UTAP does not report: coordinates, nails, label anchors,
 * comments labels, declaration comments and line numbers. The formal reading
 * of a document always comes from the compiler's strict UTAP reader.
 */
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"

namespace twin::authoring::detail {

/// @brief An element, a comment ("#comment") or a text node ("#text").
struct XmlNode {
    std::string name;                               ///< Element name, "#comment" or "#text".
    std::map<std::string, std::string> attributes;  ///< Decoded attribute values.
    std::vector<XmlNode> children;                  ///< Child nodes in document order.
    std::string text;                               ///< Decoded text (#text/#comment), or the element's direct text.
    std::uint32_t line{0};                          ///< Line of the node's start.
    std::uint32_t column{0};                        ///< Column of the node's start.
    std::uint32_t text_line{0};                     ///< Line where the element's direct text starts.

    /// @brief The first child element named @p name, or nullptr.
    [[nodiscard]] const XmlNode* child(std::string_view name) const;
    /// @brief All child elements named @p name, in order.
    [[nodiscard]] std::vector<const XmlNode*> children_named(std::string_view name) const;
    /// @brief Integer attribute, if present and well formed.
    [[nodiscard]] std::optional<std::int64_t> int_attribute(std::string_view name) const;
};

/// @brief Parse @p xml into a "#document" node whose children are the top-level comments and the root element.
[[nodiscard]] Result<XmlNode> parse_xml(std::string_view xml);

/**
 * @brief Resolve a UTAP XPath such as "/nta/template[1]/transition[3]/label[2]".
 * @return the element, or nullptr if the path does not exist.
 */
[[nodiscard]] const XmlNode* resolve_path(const XmlNode& document, std::string_view path);

}  // namespace twin::authoring::detail
