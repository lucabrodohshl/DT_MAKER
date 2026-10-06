/**
 * @file monitors.hpp
 * @brief Monitor documents (format twin-monitors/1): requirements, monitors and alert policies.
 * @ingroup monitoring
 *
 * One document per Twin Type version; it is shipped in the Verified Twin Package
 * (role "monitors"), so a recorded execution always keeps the monitor definitions
 * that applied to it. Monitor kinds and where each is evaluated:
 *
 * | kind          | parameters                                   | evaluated by |
 * |---------------|----------------------------------------------|--------------|
 * | conformance   | events ("all" or PT labels `a!`), unmatchedEvents ("reject"/"record") | the runtime (kernel) |
 * | property      | property (property.hpp)                      | runtime (locations/clocks) or Studio (semantic atoms) |
 * | data_quality  | field, check, and per check: maxAgeSeconds (stale), min/max (out_of_range), maxSilenceSeconds/source (disconnected) | the ingestion layer |
 *
 * Alert policies map a monitor result (violated, inconclusive, unknown, finding,
 * satisfied) to an alert severity and message; they are evaluated by the
 * platform, outside the kernel. Numbers are integers or decimal strings
 * ("180.5"): documents are canonical-safe JSON.
 *
 * Finding codes: TWN001 duplicate id, TWN002 unknown kind, TWN003 bad severity,
 * TWN004 parameter not allowed for the kind, TWN010 bad conformance
 * parameters, TWN011 missing or unparsable property, TWN020 bad data-quality
 * parameters, TWN030 bad alert policy, TWN040 bad requirement.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"

namespace twin::monitoring {

/// @brief Format tag of monitor documents.
inline constexpr std::string_view kMonitorsFormat = "twin-monitors/1";

/// @brief A requirement of the Twin Type (safety, mission, performance, timing, operational).
struct Requirement {
    std::string id;                     ///< e.g. "REQ-S1".
    std::string title;                  ///< Short title.
    std::string description;            ///< Free text.
    std::string category;               ///< "safety", "mission", "performance", "timing" or "operational".
    std::string severity;               ///< "info", "warning" or "critical".
    std::vector<std::string> monitors;  ///< Monitors that check it.
    std::string formal;                 ///< Optional formal expression (property syntax).
};

/// @brief A monitor definition (kind-specific parameters in @ref config).
struct MonitorSpec {
    std::string id;           ///< Unique id.
    std::string kind;         ///< "conformance", "property" or "data_quality".
    std::string name;         ///< Display name.
    std::string severity;     ///< "info", "warning" or "critical".
    std::string description;  ///< Free text (optional).
    std::string requirement;  ///< Requirement id it serves (optional).
    json::Json config = json::Json::object();  ///< Kind-specific parameters (flat keys in the document).
};

/// @brief An alert policy.
struct AlertPolicy {
    std::string id;        ///< Unique id.
    std::string monitor;   ///< Monitor id.
    std::string on;        ///< "violated", "inconclusive", "unknown", "finding" or "satisfied".
    std::string severity;  ///< Alert severity.
    std::string message;   ///< Operator-facing message.
};

/// @brief A monitor document.
struct MonitorsDocument {
    std::vector<Requirement> requirements;  ///< Requirements.
    std::vector<MonitorSpec> monitors;      ///< Monitors.
    std::vector<AlertPolicy> alerts;        ///< Alert policies.
};

/// @brief Decode a twin-monitors/1 document (ValidationError for malformed structure or floating-point numbers).
[[nodiscard]] Result<MonitorsDocument> monitors_from_json(const json::Json& document);
/// @brief Encode a document (canonical-safe).
[[nodiscard]] json::Json to_json(const MonitorsDocument& document);

/// @brief A structural finding about a monitor document.
struct Finding {
    std::string severity;  ///< "error" or "warning".
    std::string code;      ///< TWN0xx.
    std::string message;   ///< Explanation.
    std::string path;      ///< e.g. "monitors[2].check".
};
/// @brief All structural findings (empty = valid).
[[nodiscard]] std::vector<Finding> validate_document(const MonitorsDocument& document);
/// @brief API form of findings.
[[nodiscard]] json::Json to_json(const std::vector<Finding>& findings);

/// @brief The monitor @p id, or nullptr.
[[nodiscard]] const MonitorSpec* find_monitor(const MonitorsDocument& document, std::string_view id);

}  // namespace twin::monitoring
