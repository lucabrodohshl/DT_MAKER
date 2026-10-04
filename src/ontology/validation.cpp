/**
 * @file validation.cpp
 * @brief Ontology and interpretation validation (see validation.hpp).
 */
#include "twin/ontology/validation.hpp"

#include <algorithm>
#include <set>

#include "theory.hpp"
#include "twin/core/sha256.hpp"

namespace twin::ontology {

namespace {

bool any_error(const std::vector<SourceDiagnostic>& d) {
    return std::any_of(d.begin(), d.end(), [](const SourceDiagnostic& x) { return x.severity == Severity::Error; });
}

std::vector<std::string> axiom_formulas(const OntologySource& s) {
    std::vector<std::string> out;
    out.reserve(s.axioms.size());
    for (const auto& a : s.axioms) out.push_back(a.formula);
    return out;
}

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& i : items) out += (out.empty() ? "" : ", ") + i;
    return out;
}

/// @brief Symbols declared by the strict reading (functions + relations), sorted.
std::vector<std::string> strict_symbols(const OntologySource& s) {
    std::vector<std::string> out;
    for (const auto& f : s.functions) out.push_back(f.name);
    for (const auto& r : s.relations) out.push_back(r.name);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> difference(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::vector<std::string> out;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
    return out;
}

}  // namespace

OntologyValidation validate_ontology(std::string_view text, const SolverConfig& config) {
    OntologyValidation v;
    v.content_sha256 = sha256_hex(text);
    v.checker = checker_identity();
    ParsedOntology parsed = parse_ontology(text);
    v.structure = std::move(parsed.source);
    v.diagnostics = std::move(parsed.diagnostics);

    auto theory = detail::Theory::load(text);
    if (!theory) {
        v.diagnostics.push_back({"ONT100", Severity::Error, theory.error().message, {}});
        v.valid = false;
        return v;
    }
    // Cross-check: the aligner and the strict parser must see the same signature.
    const auto aligner_syms = theory.value()->symbol_names();
    const auto strict_syms = strict_symbols(v.structure);
    if (aligner_syms != strict_syms) {
        const auto only_aligner = difference(aligner_syms, strict_syms);
        const auto only_strict = difference(strict_syms, aligner_syms);
        v.diagnostics.push_back({"ONT101", Severity::Error,
                                 "the aligner reads a different signature than shown: only aligner {" +
                                     join(only_aligner) + "}, only structural reading {" + join(only_strict) + "}",
                                 {}});
    }
    if (any_error(v.diagnostics)) {
        v.valid = false;
        return v;
    }
    if (auto st = theory.value()->set_axioms(axiom_formulas(v.structure)); !st) {
        v.diagnostics.push_back({"ONT100", Severity::Error, st.error().message, {}});
        v.valid = false;
        return v;
    }
    std::string reason;
    v.consistent = theory.value()->satisfiable({}, config.timeout_ms, &reason);
    v.consistency_checked = true;
    if (v.consistent == Verdict3::False) {
        v.diagnostics.push_back({"ONT110", Severity::Error,
                                 "the axioms are inconsistent (unsatisfiable): an inconsistent ontology entails every "
                                 "formula and would make alignment and refinement checks vacuous",
                                 {}});
    } else if (v.consistent == Verdict3::Unknown) {
        v.diagnostics.push_back({"ONT111", Severity::Warning,
                                 "Z3 could not decide whether the axioms are consistent (" + reason + ")", {}});
    }
    v.valid = !any_error(v.diagnostics);
    return v;
}

InterpretationValidation validate_interpretation(std::string_view ontology_text, std::string_view interpretation_text,
                                                 const SolverConfig& config) {
    InterpretationValidation v;
    v.content_sha256 = sha256_hex(interpretation_text);
    v.ontology_sha256 = sha256_hex(ontology_text);
    v.checker = checker_identity();

    ParsedOntology ont = parse_ontology(ontology_text);
    auto theory = detail::Theory::load(ontology_text);
    if (!ont.ok() || !theory) {
        v.diagnostics.push_back({"INT100", Severity::Error,
                                 "the ontology this interpretation refers to is not valid; validate the ontology first",
                                 {}});
        return v;
    }
    ParsedInterpretation parsed = parse_interpretation(interpretation_text, &ont.source);
    v.structure = std::move(parsed.source);
    v.diagnostics = std::move(parsed.diagnostics);

    auto aligner_reading = theory.value()->read_interpretation(interpretation_text);
    if (!aligner_reading) {
        v.diagnostics.push_back({"INT100", Severity::Error, aligner_reading.error().message, {}});
        return v;
    }
    if (any_error(v.diagnostics)) return v;

    if (auto st = theory.value()->set_axioms(axiom_formulas(ont.source)); !st) {
        v.diagnostics.push_back({"INT100", Severity::Error, st.error().message, {}});
        return v;
    }
    for (const auto& [key, formula] : aligner_reading.value()) {
        const InterpretationEntry* entry = v.structure.find(key);
        const Span where = entry != nullptr ? entry->formula_span : Span{};
        std::string reason;
        const Verdict3 sat = theory.value()->satisfiable({formula}, config.timeout_ms, &reason);
        if (sat == Verdict3::False) {
            v.diagnostics.push_back({"INT110", Severity::Warning,
                                     "'" + key + "' is unsatisfiable under the ontology axioms: it can never hold",
                                     where});
            continue;
        }
        const auto valid = theory.value()->entails(formula, config.timeout_ms);
        if (valid.verdict == Verdict3::True) {
            const bool is_event = !key.empty() && key.back() == '!';
            v.diagnostics.push_back(
                {"INT111", is_event ? Severity::Warning : Severity::Note,
                 "'" + key + "' is valid under the ontology axioms (a tautology)" +
                     std::string(is_event ? ": the aligner treats tautologically interpreted events as uninterpreted"
                                          : ": it carries no domain information"),
                 where});
        }
    }
    std::stable_sort(v.diagnostics.begin(), v.diagnostics.end(),
                     [](const SourceDiagnostic& a, const SourceDiagnostic& b) { return a.span.line < b.span.line; });
    v.valid = !any_error(v.diagnostics);
    return v;
}

}  // namespace twin::ontology
