// Internal: the domain-theory compiler — K, I_D and I_P (04-compilation.md, section 4.4).
#pragma once

#include <string>
#include <vector>

#include "behavior.hpp"
#include "resolve.hpp"

namespace twin::model::detail {

struct TheoryOutput {
    std::string ontology;           // .ont
    std::string dt_interpretation;  // .interp of V_D
    std::string pt_interpretation;  // .interp of V_P ("" without a reference)
    json::Json trace = json::Json::array();
    std::size_t symbols{0};
    std::size_t axioms{0};
};

[[nodiscard]] TheoryOutput compile_theory(const Resolved& model, const BehaviorOutput& behavior, std::vector<Diagnostic>& diagnostics);

/// The ontology symbol of a property of an instance.
[[nodiscard]] std::string symbol_of(const Instance& owner, const Property& property);

}  // namespace twin::model::detail
