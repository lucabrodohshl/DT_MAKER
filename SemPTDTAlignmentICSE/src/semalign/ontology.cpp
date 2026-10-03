/**
 * @file ontology.cpp
 * @brief Implementation of the Ontology class (Definition 5: K = (S, F, R, Δ)).
 */

#include "dtpta/ontology.h"
#include <stdexcept>
#include <algorithm>

namespace dtpta {

// -------------------------------------------------------------------------- //
//  Construction / destruction
// -------------------------------------------------------------------------- //

Ontology::Ontology()
    : ctx_(std::make_unique<z3::context>())
    , solver_(std::make_unique<z3::solver>(*ctx_))
{}

Ontology::~Ontology() = default;

// -------------------------------------------------------------------------- //
//  Context access
// -------------------------------------------------------------------------- //

z3::context& Ontology::get_context() {
    return *ctx_;
}

const z3::context& Ontology::get_context() const {
    return *ctx_;
}

// -------------------------------------------------------------------------- //
//  Schema builders
// -------------------------------------------------------------------------- //

void Ontology::add_sort(const std::string& name) {
    // Built-in sorts (Real, Int, Bool) must not be registered as uninterpreted sorts.
    // resolve_sort() already handles them directly via ctx_->real_sort() etc.
    if (name == "Real" || name == "real" ||
        name == "Int"  || name == "int"  ||
        name == "Bool" || name == "bool") {
        sorts_.push_back({name});
        return;
    }
    z3::sort s = ctx_->uninterpreted_sort(name.c_str());
    sort_map_.emplace_back(name, s);
    sorts_.push_back({name});
}

void Ontology::add_function(const std::string& name,
                             const std::vector<std::string>& arg_sorts,
                             const std::string& return_sort)
{
    // Build domain sort vector
    z3::sort_vector domain(*ctx_);
    for (const auto& s : arg_sorts) {
        domain.push_back(resolve_sort(s));
    }
    z3::sort range = resolve_sort(return_sort);

    z3::func_decl fd = ctx_->function(name.c_str(), domain, range);
    func_decl_map_.emplace_back(name, fd);
    functions_.push_back({name, arg_sorts, return_sort});
}

void Ontology::add_relation(const std::string& name,
                             const std::vector<std::string>& arg_sorts)
{
    z3::sort_vector domain(*ctx_);
    for (const auto& s : arg_sorts) {
        domain.push_back(resolve_sort(s));
    }
    z3::sort bool_sort = ctx_->bool_sort();

    z3::func_decl fd = ctx_->function(name.c_str(), domain, bool_sort);
    func_decl_map_.emplace_back(name, fd);
    relations_.push_back({name, arg_sorts});
}

void Ontology::add_axiom(const z3::expr& axiom) {
    axioms_.push_back(axiom);
    solver_->add(axiom);
}

// -------------------------------------------------------------------------- //
//  Entailment checks
// -------------------------------------------------------------------------- //

bool Ontology::entails(const z3::expr& phi) {
    ++smt_calls_;
    solver_->push();
    solver_->add(!phi);
    z3::check_result r = solver_->check();
    solver_->pop();
    return (r == z3::unsat);
}

bool Ontology::entails_iff(const z3::expr& lhs, const z3::expr& rhs) {
    // Δ ⊨ lhs ↔ rhs   ⟺   Δ ∧ ¬(lhs ↔ rhs) is UNSAT
    // Build iff as (lhs → rhs) ∧ (rhs → lhs)
    z3::expr iff_expr = (implies(lhs, rhs) && implies(rhs, lhs));
    return entails(iff_expr);
}

// -------------------------------------------------------------------------- //
//  Schema accessors
// -------------------------------------------------------------------------- //

z3::sort Ontology::get_sort(const std::string& name) const {
    for (const auto& [k, v] : sort_map_) {
        if (k == name) return v;
    }
    throw std::out_of_range("Ontology: sort not found: " + name);
}

z3::func_decl Ontology::get_func_decl(const std::string& name) const {
    for (const auto& [k, v] : func_decl_map_) {
        if (k == name) return v;
    }
    throw std::out_of_range("Ontology: function/relation not found: " + name);
}

z3::func_decl Ontology::get_any_decl(const std::string& name) const {
    return get_func_decl(name);
}

// -------------------------------------------------------------------------- //
//  Internal helpers
// -------------------------------------------------------------------------- //

z3::sort Ontology::resolve_sort(const std::string& sort_name) const {
    // Built-in sorts
    if (sort_name == "Int"  || sort_name == "int")  return ctx_->int_sort();
    if (sort_name == "Real" || sort_name == "real") return ctx_->real_sort();
    if (sort_name == "Bool" || sort_name == "bool") return ctx_->bool_sort();

    // User-defined sorts
    for (const auto& [k, v] : sort_map_) {
        if (k == sort_name) return v;
    }
    throw std::out_of_range("Ontology: unknown sort '" + sort_name + "'");
}

} // namespace dtpta
