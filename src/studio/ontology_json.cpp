/**
 * @file ontology_json.cpp
 * @brief JSON renderings of ontology results (see ontology_json.hpp).
 */
#include "ontology_json.hpp"

namespace twin::studio {

using namespace twin::ontology;

json::Json to_json(const Span& s) { return {{"line", s.line}, {"column", s.column}, {"length", s.length}}; }

json::Json to_json(const SourceDiagnostic& d) {
    return {{"code", d.code}, {"severity", to_string(d.severity)}, {"message", d.message}, {"span", to_json(d.span)}};
}

json::Json to_json(const std::vector<SourceDiagnostic>& diagnostics) {
    json::Json a = json::Json::array();
    for (const auto& d : diagnostics) a.push_back(to_json(d));
    return a;
}

namespace {
json::Json symbols_of(const std::string& formula) {
    return formula_symbols(formula).value_or(std::vector<std::string>{});
}
}  // namespace

json::Json to_json(const OntologySource& s) {
    json::Json sorts = json::Json::array();
    for (const auto& x : s.sorts) sorts.push_back({{"name", x.name}, {"comment", x.comment}, {"span", to_json(x.span)}});
    json::Json functions = json::Json::array();
    for (const auto& x : s.functions) {
        functions.push_back({{"name", x.name},
                             {"argSorts", x.arg_sorts},
                             {"returnSort", x.return_sort},
                             {"signature", signature(x)},
                             {"comment", x.comment},
                             {"span", to_json(x.span)}});
    }
    json::Json relations = json::Json::array();
    for (const auto& x : s.relations) {
        relations.push_back({{"name", x.name},
                             {"argSorts", x.arg_sorts},
                             {"signature", signature(x)},
                             {"comment", x.comment},
                             {"span", to_json(x.span)}});
    }
    json::Json axioms = json::Json::array();
    for (const auto& x : s.axioms) {
        axioms.push_back({{"id", x.id},
                          {"formula", x.formula},
                          {"comment", x.comment},
                          {"symbols", symbols_of(x.formula)},
                          {"span", to_json(x.span)},
                          {"formulaSpan", to_json(x.formula_span)}});
    }
    return {{"headerComment", s.header_comment},
            {"sorts", sorts},
            {"functions", functions},
            {"relations", relations},
            {"axioms", axioms}};
}

json::Json to_json(const InterpretationSource& s) {
    json::Json entries = json::Json::array();
    for (const auto& e : s.entries) {
        entries.push_back({{"key", e.key},
                           {"isEvent", e.is_event},
                           {"formula", e.formula},
                           {"comment", e.comment},
                           {"symbols", symbols_of(e.formula)},
                           {"span", to_json(e.span)},
                           {"formulaSpan", to_json(e.formula_span)}});
    }
    return {{"headerComment", s.header_comment}, {"entries", entries}};
}

json::Json to_json(const StructuralDiff& d) {
    json::Json changes = json::Json::array();
    for (const auto& c : d.changes) {
        changes.push_back({{"kind", to_string(c.kind)},
                           {"element", c.element},
                           {"name", c.name},
                           {"previousName", c.previous_name},
                           {"before", c.before},
                           {"after", c.after}});
    }
    return {{"changes", changes},
            {"affectedSymbols", std::vector<std::string>(d.affected_symbols.begin(), d.affected_symbols.end())}};
}

json::Json to_json(const RefinementReport& r) {
    json::Json conditions = json::Json::array();
    for (const auto& c : r.conditions) {
        conditions.push_back(
            {{"condition", c.condition}, {"title", c.title}, {"status", to_string(c.status)}, {"details", c.details}});
    }
    json::Json obligations = json::Json::array();
    for (const auto& o : r.obligations) {
        json::Json model = json::Json::array();
        for (const auto& [sym, val] : o.counter_model) model.push_back({{"symbol", sym}, {"value", val}});
        obligations.push_back({{"condition", o.condition},
                               {"subject", o.subject},
                               {"statement", o.statement},
                               {"status", to_string(o.status)},
                               {"counterModel", model},
                               {"note", o.note}});
    }
    return {{"format", "twin-refinement-evidence/1"},
            {"definition", "Def. 4 (ICSE_DT_final.pdf, Section V)"},
            {"verdict", to_string(r.verdict)},
            {"summary", r.summary},
            {"conditions", conditions},
            {"obligations", obligations},
            {"assumptions", r.assumptions},
            {"failureReasons", r.failure_reasons},
            {"checker", r.checker},
            {"timeoutMs", r.timeout_ms},
            {"hashes",
             {{"baseOntology", r.base_ontology_sha256},
              {"candidateOntology", r.candidate_ontology_sha256},
              {"basePtInterpretation", r.base_pt_sha256},
              {"candidatePtInterpretation", r.candidate_pt_sha256},
              {"baseDtInterpretation", r.base_dt_sha256},
              {"candidateDtInterpretation", r.candidate_dt_sha256}}}};
}

json::Json to_json(const EvaluationReport& r) {
    json::Json entries = json::Json::array();
    for (const auto& e : r.entries) {
        entries.push_back({{"key", e.key},
                           {"isEvent", e.is_event},
                           {"formula", e.formula},
                           {"truth", to_string(e.truth)},
                           {"symbols", e.symbols},
                           {"unobserved", e.unobserved},
                           {"solverNote", e.solver_note}});
    }
    return {{"observationsConsistent", to_string(r.observations_consistent)},
            {"entries", entries},
            {"ontologySha256", r.ontology_sha256},
            {"interpretationSha256", r.interpretation_sha256},
            {"checker", r.checker}};
}

json::Json to_json(const OntologyValidation& v) {
    return {{"format", "twin-ontology-validation/1"},
            {"valid", v.valid},
            {"consistent", to_string(v.consistent)},
            {"consistencyChecked", v.consistency_checked},
            {"diagnostics", to_json(v.diagnostics)},
            {"contentSha256", v.content_sha256},
            {"checker", v.checker}};
}

json::Json to_json(const InterpretationValidation& v) {
    return {{"format", "twin-interpretation-validation/1"},
            {"valid", v.valid},
            {"diagnostics", to_json(v.diagnostics)},
            {"contentSha256", v.content_sha256},
            {"ontologySha256", v.ontology_sha256},
            {"checker", v.checker}};
}

}  // namespace twin::studio
