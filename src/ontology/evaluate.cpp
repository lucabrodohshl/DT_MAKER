/**
 * @file evaluate.cpp
 * @brief Three-valued interpretation evaluation (see evaluate.hpp).
 */
#include "twin/ontology/evaluate.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include "theory.hpp"
#include "twin/core/sha256.hpp"
#include "twin/ontology/source.hpp"

namespace twin::ontology {

std::string_view to_string(Truth t) noexcept {
    switch (t) {
        case Truth::True: return "true";
        case Truth::False: return "false";
        case Truth::Unknown: return "unknown";
        case Truth::InconsistentObservation: return "inconsistent_observation";
    }
    return "unknown";
}

namespace {

/// @brief Exact decimal: -?digits(.digits)?
bool is_exact_decimal(std::string_view v) noexcept {
    std::size_t i = 0;
    if (i < v.size() && v[i] == '-') ++i;
    const std::size_t int_start = i;
    while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) ++i;
    if (i == int_start) return false;
    if (i == v.size()) return true;
    if (v[i] != '.') return false;
    ++i;
    const std::size_t frac_start = i;
    while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) ++i;
    return i == v.size() && i > frac_start;
}

/// @brief SMT-LIB2 literal for a decimal (negative numbers as (- x)).
std::string smt_literal(const std::string& v) {
    if (!v.empty() && v[0] == '-') return "(- " + v.substr(1) + ")";
    return v;
}

}  // namespace

Result<EvaluationReport> evaluate_interpretation(std::string_view ontology_text, std::string_view interpretation_text,
                                                 std::span<const Observation> observations,
                                                 const std::vector<std::string>& keys, const SolverConfig& config) {
    ParsedOntology ont = parse_ontology(ontology_text);
    if (!ont.ok()) return make_error(ErrorCode::ParseError, "the ontology is not valid; validate it first");
    ParsedInterpretation interp = parse_interpretation(interpretation_text, &ont.source);
    if (!interp.ok()) return make_error(ErrorCode::ParseError, "the interpretation is not valid; validate it first");

    auto loaded = detail::Theory::load(ontology_text);
    if (!loaded) return std::move(loaded).error();
    detail::Theory& theory = *loaded.value();
    std::vector<std::string> axioms;
    for (const auto& a : ont.source.axioms) axioms.push_back(a.formula);
    if (auto st = theory.set_axioms(axioms); !st) return st.error();
    auto reading = theory.read_interpretation(interpretation_text);
    if (!reading) return std::move(reading).error();

    // Observations -> assumptions.
    std::vector<std::string> obs_formulas;
    std::set<std::string> observed;
    for (const auto& o : observations) {
        const FunctionDecl* f = ont.source.find_function(o.symbol);
        const RelationDecl* r = ont.source.find_relation(o.symbol);
        if (f != nullptr && f->arg_sorts.empty()) {
            if (!is_exact_decimal(o.value)) {
                return make_error(ErrorCode::InvalidArgument, "observation value must be an exact decimal")
                    .with("symbol", o.symbol)
                    .with("value", o.value);
            }
            obs_formulas.push_back("(= " + o.symbol + " " + smt_literal(o.value) + ")");
        } else if (r != nullptr && r->arg_sorts.empty()) {
            if (o.value != "true" && o.value != "false") {
                return make_error(ErrorCode::InvalidArgument, "observation of a relation must be true or false")
                    .with("symbol", o.symbol);
            }
            obs_formulas.push_back(o.value == "true" ? o.symbol : "(not " + o.symbol + ")");
        } else {
            return make_error(ErrorCode::InvalidArgument,
                              "observations must bind a nullary function or relation of the ontology")
                .with("symbol", o.symbol);
        }
        if (!observed.insert(o.symbol).second) {
            return make_error(ErrorCode::InvalidArgument, "symbol observed twice").with("symbol", o.symbol);
        }
    }
    auto obs = theory.parse_formulas(obs_formulas);
    if (!obs) return std::move(obs).error();

    EvaluationReport report;
    report.ontology_sha256 = sha256_hex(ontology_text);
    report.interpretation_sha256 = sha256_hex(interpretation_text);
    report.checker = checker_identity();
    std::string reason;
    report.observations_consistent = theory.satisfiable(obs.value(), config.timeout_ms, &reason);

    for (const auto& k : keys) {
        if (interp.source.find(k) == nullptr) {
            return make_error(ErrorCode::NotFound, "no interpretation entry with this key").with("key", k);
        }
    }
    for (const auto& entry : interp.source.entries) {
        if (!keys.empty() && std::find(keys.begin(), keys.end(), entry.key) == keys.end()) continue;
        EntryEvaluation e;
        e.key = entry.key;
        e.is_event = entry.is_event;
        e.formula = entry.formula;
        e.symbols = formula_symbols(entry.formula).value_or(std::vector<std::string>{});
        for (const auto& s : e.symbols) {
            if (observed.count(s) == 0 && ont.source.find_sort(s) == nullptr) e.unobserved.push_back(s);
        }
        const auto it = std::find_if(reading.value().begin(), reading.value().end(),
                                     [&](const auto& p) { return p.first == entry.key; });
        if (it == reading.value().end()) {
            return make_error(ErrorCode::Internal, "aligner reading lacks an entry the structural reading has")
                .with("key", entry.key);
        }
        if (report.observations_consistent == Verdict3::False) {
            e.truth = Truth::InconsistentObservation;
        } else if (report.observations_consistent == Verdict3::Unknown) {
            // Entailment from a possibly inconsistent premise could be vacuous: do not claim it.
            e.truth = Truth::Unknown;
            e.solver_note = "consistency of the observations could not be decided: " + reason;
        } else {
            const auto pos = theory.entails_under(obs.value(), it->second, config.timeout_ms);
            if (pos.verdict == Verdict3::True) {
                e.truth = Truth::True;
            } else {
                const auto neg = theory.entails_under(obs.value(), !it->second, config.timeout_ms);
                if (neg.verdict == Verdict3::True) {
                    e.truth = Truth::False;
                } else {
                    e.truth = Truth::Unknown;
                    if (pos.verdict == Verdict3::Unknown) e.solver_note = pos.reason;
                    else if (neg.verdict == Verdict3::Unknown) e.solver_note = neg.reason;
                }
            }
        }
        report.entries.push_back(std::move(e));
    }
    return report;
}

}  // namespace twin::ontology
