/**
 * @file compile.hpp
 * @brief Compilation of a twin model into every derived representation.
 * @ingroup model
 *
 * One call turns a twin-model/1 document into:
 *  - the Digital Twin View V_D as a canonical timed automaton (twin-ta/1) and
 *    its UPPAAL toolchain XML, inside the verified fragment by construction;
 *  - the formal domain theory K (.ont) and the interpretation I_D (.interp),
 *    and, when the model has a physical reference, V_P and I_P;
 *  - the OWL 2 + SHACL schema and the RDF knowledge graph (Turtle, JSON-LD);
 *  - the Blueprint sections derived from the model (structure, data contract,
 *    presentation, monitors);
 *  - a trace from every generated element to the model elements and the rule
 *    that produced it.
 *
 * Compilation is a pure function of the document's semantic content and
 * kCompilerVersion: no clock, no randomness, no I/O. Rules are numbered as in
 * docs/superpowers/specs/2026-10-15-model-once-studio/04-compilation.md.
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/authoring/model.hpp"
#include "twin/json/canonical.hpp"
#include "twin/model/document.hpp"

namespace twin::model {

/// @brief Identity of the compiler (part of every derived artefact's provenance).
inline constexpr std::string_view kCompilerVersion = "twin-model-compiler/1";

/// @brief Largest product automaton the compiler emits (TML060 above).
inline constexpr std::size_t kMaxLocations = 2000;

/// @brief Everything derived from a valid model.
struct Artifacts {
    std::optional<authoring::Model> automaton;  ///< V_D (absent when no asset has a behaviour).
    std::string automaton_xml;                  ///< Toolchain UPPAAL XML of V_D.
    std::string ontology;                       ///< K in the .ont format.
    std::string dt_interpretation;              ///< I_D in the .interp format.
    std::optional<authoring::Model> reference;  ///< V_P (the physical reference), when present.
    std::string reference_xml;                  ///< Toolchain UPPAAL XML of V_P.
    std::string pt_interpretation;              ///< I_P, when a reference is present.
    std::string schema_turtle;                  ///< OWL 2 + SHACL schema (Turtle).
    std::string graph_turtle;                   ///< RDF knowledge graph of the model (Turtle).
    json::Json graph_jsonld;                    ///< Schema and graph as JSON-LD.
    json::Json sections;                        ///< Derived Blueprint sections {structure, data, presentation, assurance}.
    json::Json trace;                           ///< Generated element -> sources.
    json::Json summary;                         ///< Counts (locations, edges, clocks, symbols, triples…).
};

/// @brief Result of compile().
struct Compiled {
    std::vector<Diagnostic> diagnostics;   ///< Every finding (decoding, validation, compilation).
    std::optional<Artifacts> artifacts;    ///< Present iff there is no error.
    std::string semantic_digest;           ///< Digest of the document's semantic content.
};

/// @brief Compile a typed document.
[[nodiscard]] Compiled compile(const Document& document);

/// @brief Decode and compile a twin-model/1 JSON document.
[[nodiscard]] Compiled compile(const json::Json& document);

/// @brief Validate only (no artefacts): decoding and validation diagnostics.
[[nodiscard]] std::vector<Diagnostic> check(const json::Json& document);

/// @brief JSON form of the artefacts (texts, documents, trace, summary) for the API and the CLI.
[[nodiscard]] json::Json to_json(const Artifacts& artifacts);

}  // namespace twin::model
