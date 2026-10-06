/**
 * @file reader.hpp
 * @brief The compiler's strict reading of a UPPAAL document (shared with the Studio importer).
 * @ingroup compiler
 *
 * SourceModel is the compiler's faithful, already-checked reading of a UPPAAL
 * document: exactly the abstract syntax  A = (L, l0, C, Sigma, E, Inv)  of
 * proof/sections/02-source-semantics.tex. Indices follow document order, which
 * is also the order the aligner uses (locations, edges, clocks).
 *
 * read_uppaal() is the same strict reader compile_file() uses: every construct
 * outside the supported fragment is reported with its TWC code (all of them,
 * not only the first), and a model is returned only when there is none. The
 * Studio importer (twin::authoring) builds its canonical model from this
 * reading, so importing never interprets a document differently from compiling it.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "twin/compiler/diagnostics.hpp"
#include "twin/ir/model.hpp"

namespace twin::compiler {

/// @brief An atomic clock constraint as read from the source, with its source text.
struct SourceAtom {
    ir::ClockConstraint constraint;              ///< Clock ids follow the aligner's numbering (1-based).
    std::string text;                            ///< Source rendering, for diagnostics.
    std::optional<std::string> bound_constant;   ///< Name of the constant when the bound is a single identifier.
};

/// @brief A location of the source automaton.
struct SourceLocation {
    std::string name;                    ///< UTAP symbol name (= aligner location name).
    std::vector<SourceAtom> invariant;   ///< Conjunction (source order).
};

/// @brief An edge of the source automaton.
struct SourceEdge {
    std::size_t source{0};                        ///< Source location index.
    std::size_t target{0};                        ///< Target location index.
    ir::Action action;                            ///< tau or channel + direction.
    std::vector<SourceAtom> guard;                ///< Conjunction (source order).
    std::vector<ir::ClockIndex> resets;           ///< Reset clocks (source order).
    std::string where;                            ///< Human-readable position for diagnostics.
};

/// @brief The complete validated source model.
struct SourceModel {
    std::string template_name;            ///< UPPAAL template name.
    std::vector<std::string> clocks;      ///< Global clocks first, then template clocks.
    std::vector<std::pair<std::string, std::int64_t>> constants;  ///< Integer constants, declaration order.
    std::vector<std::string> channels;    ///< Declared plain channels, declaration order.
    std::vector<SourceLocation> locations;  ///< Document order.
    std::size_t initial{0};               ///< Index of the `init` location.
    std::vector<SourceEdge> edges;        ///< Document order.
};

/// @brief Options of the strict reader.
struct ReaderOptions {
    bool legacy_system_declaration{false};  ///< See CompileOptions::legacy_system_declaration.
};

/// @brief Outcome of read_uppaal().
struct ReadResult {
    std::optional<SourceModel> model;      ///< Present iff no error was found.
    std::vector<Diagnostic> diagnostics;   ///< Every problem found (errors, warnings).
};

/// @brief Parse @p xml with UTAP and read it strictly (see file documentation).
[[nodiscard]] ReadResult read_uppaal(std::string_view xml, const ReaderOptions& options = {});

}  // namespace twin::compiler
