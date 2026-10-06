/**
 * @file properties.hpp
 * @brief Property analysis (what can be checked or monitored) and design-time property checks.
 * @ingroup authoring
 *
 * Monitorability is decided here, from what the backend can actually do:
 *
 * | form | design time | run time | evaluator |
 * |------|-------------|----------|-----------|
 * | `A[] φ`, φ over locations/clocks | checkable (exact zone graph) | monitorable | runtime |
 * | `E<> φ`, φ over locations/clocks | checkable | not monitorable | — |
 * | `A[] φ`, φ with `sem(…)` and locations, no clocks | guarantee check (Δ ⊨ I_D(L) → φ_L for every reachable L) | monitorable | Studio |
 * | `A<>`, `E[]`, `-->`; `sem(…)` with clocks; `E<>` with `sem(…)` | unsupported | unsupported | — |
 *
 * Design-time checks run on V_D's dense-time zone graph from the zero valuation
 * (exact for diagonal-free models with k-extrapolation including the property's
 * constants; models with diagonal constraints are reported unsupported). An `A[]`
 * that holds in dense time also holds on the kernel's logical-time grid. The
 * evidence document (twin-property-evidence/1) contains no timings and is
 * reproducible.
 *
 * Diagnostic codes: TWP001 syntax, TWP010 unknown location, TWP011 unknown clock,
 * TWP012 semantic formula not well formed over the ontology, TWP013 semantic
 * formula not checked (no ontology given); monitor references: TWN012 unknown PT
 * label, TWN021 unknown telemetry field.
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/authoring/diagnostics.hpp"
#include "twin/authoring/model.hpp"
#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/monitoring/monitors.hpp"

namespace twin::authoring {

/// @brief What the backend can do with a property (see file documentation).
struct PropertyAnalysis {
    bool valid{false};                  ///< Parses and every name resolves.
    std::vector<Diagnostic> diagnostics;  ///< TWP0xx.
    std::string text;                   ///< Canonical text of the property (if it parses).
    std::string quantifier;             ///< "A[]", "E<>", "A<>", "E[]" or "-->".
    std::vector<std::string> locations;  ///< Locations used.
    std::vector<std::string> clocks;    ///< Clocks used.
    std::vector<std::string> semantic;  ///< Semantic (SMT-LIB2) atoms used.
    std::string design_time;            ///< "checkable", "guarantee_check" or "unsupported".
    std::string runtime;                ///< "monitorable", "not_monitorable" or "unsupported".
    std::string evaluator;              ///< "runtime", "studio" or "none".
    std::vector<std::string> reasons;   ///< Why something is unsupported or limited.
};

/// @brief Analyse @p text against the DT model (and the ontology, to check semantic atoms).
[[nodiscard]] PropertyAnalysis analyse_property(std::string_view text, const Model& dt,
                                                const std::optional<std::string>& ontology_text);
/// @brief API form of an analysis.
[[nodiscard]] json::Json to_json(const PropertyAnalysis& analysis);

/// @brief Inputs of a design-time property check.
struct PropertyCheckInputs {
    std::string property;                              ///< Property text.
    std::string dt_xml;                                ///< V_D (toolchain bytes).
    std::optional<std::string> ontology_text;          ///< K (for semantic properties).
    std::optional<std::string> dt_interpretation_text; ///< I_D (for semantic properties).
    std::size_t max_states{200000};                    ///< Zone-graph state limit.
};

/**
 * @brief Check a property at design time; returns twin-property-evidence/1:
 * {format, property, quantifier, verdict, method, semantics, witness?, locations?,
 * states, complete, reasons, inputs, checker}. Verdicts: holds / violated (A[]),
 * holds / does_not_hold (E<>), guaranteed / not_guaranteed (semantic),
 * inconclusive (state limit), unsupported.
 */
[[nodiscard]] Result<json::Json> check_property(const PropertyCheckInputs& inputs);

/**
 * @brief Validate a monitor document against the views and the telemetry schema
 * ([{id, ...}] fields). Structural findings (TWN0xx) plus references.
 */
[[nodiscard]] std::vector<Diagnostic> validate_monitors(const monitoring::MonitorsDocument& document, const Model* pt,
                                                        const Model* dt, const json::Json& telemetry_schema,
                                                        const std::optional<std::string>& ontology_text);

}  // namespace twin::authoring
