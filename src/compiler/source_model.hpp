/**
 * @file source_model.hpp
 * @brief Internal: the validated abstract syntax of a source timed automaton.
 *
 * SourceModel is the compiler's faithful, already-checked reading of the UPPAAL
 * document: exactly the abstract syntax  A = (L, l0, C, Sigma, E, Inv)  of
 * proof/sections/02-source-semantics.tex. Indices follow document order, which
 * is also the order the aligner uses (locations, edges, clocks).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "twin/ir/model.hpp"

namespace twin::compiler::detail {

/// @brief An atomic clock constraint as read from the source, with its source text.
struct SourceAtom {
    ir::ClockConstraint constraint;  ///< Clock ids follow the aligner's numbering (1-based).
    std::string text;                ///< Source rendering, for diagnostics.
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
    std::vector<std::string> channels;    ///< Declared plain channels, declaration order.
    std::vector<SourceLocation> locations;  ///< Document order.
    std::size_t initial{0};               ///< Index of the <init> location.
    std::vector<SourceEdge> edges;        ///< Document order.
};

}  // namespace twin::compiler::detail
