/**
 * @file properties.cpp
 * @brief Property analysis, design-time checks (zone graph, ontology entailment), monitor validation.
 */
#include "twin/authoring/properties.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

#include "twin/alignment/entailment.hpp"
#include "twin/compiler/compiler.hpp"
#include "twin/compiler/reader.hpp"
#include "twin/core/sha256.hpp"
#include "twin/monitoring/property.hpp"
#include "zone_graph.hpp"

namespace twin::authoring {
namespace {

using monitoring::Property;
using monitoring::Quantifier;
using monitoring::StateFormula;
using Kind = StateFormula::Kind;

Diagnostic diag(std::string severity, std::string code, std::string message, std::string hint = {}) {
    return Diagnostic{std::move(severity), std::move(code), std::move(message), std::move(hint),
                      {"document", "property", ""}, std::nullopt};
}

/// Classification of a parsed property (no name resolution).
struct Classification {
    std::string design_time;
    std::string runtime;
    std::string evaluator;
    std::vector<std::string> reasons;
};

Classification classify(const Property& p, bool diagonal_model) {
    monitoring::Atoms a = monitoring::atoms(p.phi);
    if (p.psi) {
        const monitoring::Atoms b = monitoring::atoms(*p.psi);
        a.locations.insert(b.locations.begin(), b.locations.end());
        a.clocks.insert(b.clocks.begin(), b.clocks.end());
        a.semantic.insert(a.semantic.end(), b.semantic.begin(), b.semantic.end());
    }
    const bool sem = !a.semantic.empty();
    const bool clocks = !a.clocks.empty();
    if (p.quantifier == Quantifier::Inevitably || p.quantifier == Quantifier::Potentially ||
        p.quantifier == Quantifier::LeadsTo) {
        return {"unsupported", "unsupported", "none",
                {"liveness properties (A<>, E[], -->) are not checked or monitored by this version"}};
    }
    if (sem && clocks) {
        return {"unsupported", "unsupported", "none", {"semantic atoms cannot be combined with clock constraints"}};
    }
    if (p.quantifier == Quantifier::Eventually) {
        if (sem) {
            return {"unsupported", "not_monitorable", "none",
                    {"the possibility of a semantic state is not checked; E<> is not a run-time property"}};
        }
        Classification c{"checkable", "not_monitorable", "none", {"E<> is a possibility, not a property of one run"}};
        if (diagonal_model) {
            c.design_time = "unsupported";
            c.reasons.push_back("the model has diagonal clock constraints; exact zone-graph checking is not available");
        }
        return c;
    }
    // A[]
    if (sem) return {"guarantee_check", "monitorable", "studio", {}};
    Classification c{"checkable", "monitorable", "runtime", {}};
    if (diagonal_model) {
        c.design_time = "unsupported";
        c.reasons.push_back("the model has diagonal clock constraints; exact zone-graph checking is not available");
    }
    return c;
}

bool model_has_diagonal(const Model& m) {
    const auto diagonal = [](const Constraint& c) {
        return std::any_of(c.begin(), c.end(), [](const Atom& a) { return a.minus.has_value(); });
    };
    return std::any_of(m.locations.begin(), m.locations.end(), [&](const LocationDecl& l) { return diagonal(l.invariant); }) ||
           std::any_of(m.edges.begin(), m.edges.end(), [&](const EdgeDecl& e) { return diagonal(e.guard); });
}

// ------------------------------------------------------------------ DNF over clock atoms
using Conjunct = std::vector<ir::ClockConstraint>;
using Dnf = std::vector<Conjunct>;

Dnf product(const Dnf& a, const Dnf& b) {
    Dnf out;
    for (const Conjunct& x : a) {
        for (const Conjunct& y : b) {
            Conjunct c = x;
            c.insert(c.end(), y.begin(), y.end());
            out.push_back(std::move(c));
        }
    }
    return out;
}

Dnf join(Dnf a, const Dnf& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

ir::ClockConstraint atom_of(const StateFormula& f, const std::map<std::string, ir::ClockIndex>& clocks, ir::Comparison op) {
    return ir::ClockConstraint{clocks.at(f.name), f.minus ? clocks.at(*f.minus) : ir::kReferenceClock, op, f.bound};
}

/// DNF of φ (or ¬φ when @p negate) at location @p location, over clock atoms (no semantic atoms).
Dnf dnf(const StateFormula& f, bool negate, const std::string& location, const std::map<std::string, ir::ClockIndex>& clocks) {
    const Dnf yes{Conjunct{}};
    const Dnf no{};
    switch (f.kind) {
        case Kind::True: return negate ? no : yes;
        case Kind::False: return negate ? yes : no;
        case Kind::Location: return ((f.name == location) != negate) ? yes : no;
        case Kind::Clock: {
            if (!negate) return Dnf{Conjunct{atom_of(f, clocks, f.op)}};
            switch (f.op) {
                case ir::Comparison::Less: return Dnf{Conjunct{atom_of(f, clocks, ir::Comparison::GreaterEqual)}};
                case ir::Comparison::LessEqual: return Dnf{Conjunct{atom_of(f, clocks, ir::Comparison::Greater)}};
                case ir::Comparison::GreaterEqual: return Dnf{Conjunct{atom_of(f, clocks, ir::Comparison::Less)}};
                case ir::Comparison::Greater: return Dnf{Conjunct{atom_of(f, clocks, ir::Comparison::LessEqual)}};
                case ir::Comparison::Equal:
                    return Dnf{Conjunct{atom_of(f, clocks, ir::Comparison::Less)},
                               Conjunct{atom_of(f, clocks, ir::Comparison::Greater)}};
            }
            return no;
        }
        case Kind::Semantic: return no;  // excluded by classification
        case Kind::Not: return dnf(f.children.at(0), !negate, location, clocks);
        case Kind::And: {
            const Dnf a = dnf(f.children.at(0), negate, location, clocks);
            const Dnf b = dnf(f.children.at(1), negate, location, clocks);
            return negate ? join(a, b) : product(a, b);
        }
        case Kind::Or: {
            const Dnf a = dnf(f.children.at(0), negate, location, clocks);
            const Dnf b = dnf(f.children.at(1), negate, location, clocks);
            return negate ? product(a, b) : join(a, b);
        }
        case Kind::Implies: {
            const Dnf a = dnf(f.children.at(0), !negate, location, clocks);  // ¬a (or a when negated)
            const Dnf b = dnf(f.children.at(1), negate, location, clocks);
            return negate ? product(a, b) : join(a, b);
        }
    }
    return no;
}

/// SMT-LIB2 text of φ with the location atoms replaced by their truth value at @p location.
std::string smt_at(const StateFormula& f, const std::string& location) {
    switch (f.kind) {
        case Kind::True: return "true";
        case Kind::False: return "false";
        case Kind::Location: return f.name == location ? "true" : "false";
        case Kind::Semantic: return f.formula;
        case Kind::Not: return "(not " + smt_at(f.children.at(0), location) + ")";
        case Kind::And: return "(and " + smt_at(f.children.at(0), location) + " " + smt_at(f.children.at(1), location) + ")";
        case Kind::Or: return "(or " + smt_at(f.children.at(0), location) + " " + smt_at(f.children.at(1), location) + ")";
        case Kind::Implies: return "(=> " + smt_at(f.children.at(0), location) + " " + smt_at(f.children.at(1), location) + ")";
        case Kind::Clock: return "true";  // excluded by classification
    }
    return "true";
}

json::Json witness(const detail::ZoneGraph& g, std::size_t state, const compiler::SourceModel& m) {
    std::vector<std::string> path;
    for (std::size_t s = state;; s = g.states[s].parent) {
        const std::string& name = m.locations[g.states[s].location].name;
        if (path.empty() || path.back() != name) path.push_back(name);
        if (s == 0) break;
    }
    std::reverse(path.begin(), path.end());
    return json::Json{{"path", path}, {"location", m.locations[g.states[state].location].name}};
}

}  // namespace

PropertyAnalysis analyse_property(std::string_view text, const Model& dt, const std::optional<std::string>& ontology_text) {
    PropertyAnalysis out;
    Result<Property> parsed = monitoring::parse_property(text);
    if (!parsed) {
        Diagnostic d = diag("error", "TWP001", parsed.error().message);
        const std::string col(parsed.error().context_value("column"));
        if (!col.empty()) {
            const auto c = static_cast<std::uint32_t>(std::stoul(col));
            d.range = SourceRange{1, c, 1, c};
        }
        out.diagnostics.push_back(std::move(d));
        out.design_time = "unsupported";
        out.runtime = "unsupported";
        out.evaluator = "none";
        return out;
    }
    const Property& p = parsed.value();
    out.text = monitoring::print_property(p);
    out.quantifier = std::string(monitoring::to_string(p.quantifier));
    monitoring::Atoms a = monitoring::atoms(p.phi);
    if (p.psi) {
        const monitoring::Atoms b = monitoring::atoms(*p.psi);
        a.locations.insert(b.locations.begin(), b.locations.end());
        a.clocks.insert(b.clocks.begin(), b.clocks.end());
        a.semantic.insert(a.semantic.end(), b.semantic.begin(), b.semantic.end());
    }
    out.locations.assign(a.locations.begin(), a.locations.end());
    out.clocks.assign(a.clocks.begin(), a.clocks.end());
    out.semantic = a.semantic;
    std::set<std::string> locations;
    std::set<std::string> clocks;
    for (const LocationDecl& l : dt.locations) locations.insert(l.name);
    for (const ClockDecl& c : dt.clocks) clocks.insert(c.name);
    for (const std::string& l : a.locations) {
        if (!locations.contains(l)) out.diagnostics.push_back(diag("error", "TWP010", "'" + l + "' is not a location of the DT view"));
    }
    for (const std::string& c : a.clocks) {
        if (!clocks.contains(c)) out.diagnostics.push_back(diag("error", "TWP011", "'" + c + "' is not a clock of the DT view"));
    }
    for (const std::string& f : a.semantic) {
        if (!ontology_text) {
            out.diagnostics.push_back(diag("warning", "TWP013", "sem(" + f + ") was not checked: no ontology was given"));
            continue;
        }
        if (Status s = alignment::check_formula(*ontology_text, f); !s) {
            out.diagnostics.push_back(diag("error", "TWP012", "sem(" + f + "): " + s.error().message,
                                           "use the ontology's symbols in SMT-LIB2 syntax"));
        }
    }
    const Classification c = classify(p, model_has_diagonal(dt));
    out.design_time = c.design_time;
    out.runtime = c.runtime;
    out.evaluator = c.evaluator;
    out.reasons = c.reasons;
    out.valid = !has_errors(out.diagnostics);
    return out;
}

json::Json to_json(const PropertyAnalysis& a) {
    return json::Json{{"valid", a.valid},
                      {"diagnostics", to_json(a.diagnostics)},
                      {"text", a.text},
                      {"quantifier", a.quantifier},
                      {"locations", a.locations},
                      {"clocks", a.clocks},
                      {"semantic", a.semantic},
                      {"designTime", a.design_time},
                      {"runtime", a.runtime},
                      {"evaluator", a.evaluator},
                      {"reasons", a.reasons}};
}

namespace {

/// Semantic guarantee: Δ ⊨ I_D(L) → φ_L for every reachable location L.
Result<json::Json> check_semantic(json::Json evidence, const Property& p, const detail::ZoneGraph& g,
                                  const compiler::SourceModel& m, const PropertyCheckInputs& in) {
    evidence["method"] = "ontology entailment per reachable location (dtpta::Ontology, Z3)";
    if (!in.ontology_text || !in.dt_interpretation_text) {
        return make_error(ErrorCode::InvalidArgument, "a semantic property needs the ontology and the DT interpretation");
    }
    std::set<std::string> reachable;
    for (const detail::ZoneState& s : g.states) reachable.insert(m.locations[s.location].name);
    std::map<std::string, std::string> formulas;
    for (const std::string& l : reachable) formulas[l] = smt_at(p.phi, l);
    Result<std::map<std::string, std::optional<bool>>> e =
        alignment::entailment_by_location(*in.ontology_text, *in.dt_interpretation_text, formulas);
    if (!e) return std::move(e).error();
    json::Json rows = json::Json::array();
    bool all = true;
    for (const auto& [location, entailed] : e.value()) {
        json::Json row{{"location", location}, {"guaranteed", entailed.value_or(false)}};
        if (!entailed) row["reason"] = "the location has no interpretation";
        all = all && entailed.value_or(false);
        rows.push_back(std::move(row));
    }
    evidence["locations"] = rows;
    if (!g.complete) evidence["verdict"] = "inconclusive";
    else evidence["verdict"] = all ? "guaranteed" : "not_guaranteed";
    return evidence;
}

/// A[] / E<> over locations and clocks: search the zone graph for ¬φ (A[]) or φ (E<>).
json::Json check_behavioural(json::Json evidence, const Property& p, const detail::ZoneGraph& g,
                             const compiler::SourceModel& m, const std::map<std::string, ir::ClockIndex>& clocks) {
    evidence["method"] = "exact forward zone-graph reachability (UDBM)";
    const bool always = p.quantifier == Quantifier::Always;
    for (std::size_t s = 0; s < g.states.size(); ++s) {
        const Dnf target = dnf(p.phi, /*negate=*/always, m.locations[g.states[s].location].name, clocks);
        const bool found = std::any_of(target.begin(), target.end(), [&](const Conjunct& conj) {
            return detail::satisfiable(g.states[s].zone, g.dim, conj);
        });
        if (found) {
            evidence["verdict"] = always ? "violated" : "holds";
            evidence["witness"] = witness(g, s, m);
            return evidence;
        }
    }
    if (!g.complete) {
        evidence["verdict"] = "inconclusive";
        evidence["reasons"] = json::Json::array({"the zone graph exceeded the state limit"});
        return evidence;
    }
    evidence["verdict"] = always ? "holds" : "does_not_hold";
    return evidence;
}

/// Maximal constants of the model, raised by the clock bounds of the property.
std::vector<std::int32_t> check_constants(const compiler::SourceModel& m, const StateFormula& phi,
                                          const std::map<std::string, ir::ClockIndex>& clocks) {
    std::vector<std::int32_t> max = detail::model_max_constants(m);
    std::function<void(const StateFormula&)> visit = [&](const StateFormula& f) {
        if (f.kind == Kind::Clock) {
            const auto b = static_cast<std::int32_t>(std::llabs(f.bound));
            max[clocks.at(f.name)] = std::max(max[clocks.at(f.name)], b);
            if (f.minus) max[clocks.at(*f.minus)] = std::max(max[clocks.at(*f.minus)], b);
        }
        for (const StateFormula& ch : f.children) visit(ch);
    };
    visit(phi);
    return max;
}

}  // namespace

Result<json::Json> check_property(const PropertyCheckInputs& in) {
    Result<Property> parsed = monitoring::parse_property(in.property);
    if (!parsed) {
        return make_error(ErrorCode::InvalidArgument, "the property does not parse: " + parsed.error().message);
    }
    const Property& p = parsed.value();
    const compiler::ReadResult read = compiler::read_uppaal(in.dt_xml);
    if (!read.model) return make_error(ErrorCode::InvalidArgument, "the DT view is not inside the supported fragment");
    const compiler::SourceModel& m = *read.model;
    json::Json inputs{{"dt_sha256", sha256_hex(in.dt_xml)}};
    if (in.ontology_text) inputs["ontology_sha256"] = sha256_hex(*in.ontology_text);
    if (in.dt_interpretation_text) inputs["dt_interpretation_sha256"] = sha256_hex(*in.dt_interpretation_text);
    json::Json evidence{{"format", "twin-property-evidence/1"},
                        {"property", monitoring::print_property(p)},
                        {"quantifier", std::string(monitoring::to_string(p.quantifier))},
                        {"inputs", inputs},
                        {"checker", {{"name", "twin-property-checker"}, {"version", "1"}}},
                        {"reasons", json::Json::array()}};
    const Classification c = classify(p, detail::has_diagonal(m));
    if (c.design_time == "unsupported") {
        evidence["verdict"] = "unsupported";
        evidence["reasons"] = c.reasons;
        return evidence;
    }
    std::map<std::string, ir::ClockIndex> clocks;
    for (std::size_t k = 0; k < m.clocks.size(); ++k) clocks[m.clocks[k]] = static_cast<ir::ClockIndex>(k + 1);
    std::set<std::string> locations;
    for (const compiler::SourceLocation& l : m.locations) locations.insert(l.name);
    const monitoring::Atoms a = monitoring::atoms(p.phi);
    for (const std::string& l : a.locations) {
        if (!locations.contains(l)) return make_error(ErrorCode::NotFound, "'" + l + "' is not a location of the DT view");
    }
    for (const std::string& x : a.clocks) {
        if (!clocks.contains(x)) return make_error(ErrorCode::NotFound, "'" + x + "' is not a clock of the DT view");
    }
    const detail::ZoneGraph g = detail::explore(m, check_constants(m, p.phi, clocks), in.max_states);
    evidence["states"] = static_cast<std::int64_t>(g.states.size());
    evidence["complete"] = g.complete;
    evidence["semantics"] = "dense-time zone graph of V_D from the zero valuation, k-extrapolated with the constants of "
                            "the model and the property";
    if (c.design_time == "guarantee_check") return check_semantic(std::move(evidence), p, g, m, in);
    return check_behavioural(std::move(evidence), p, g, m, clocks);
}

std::vector<Diagnostic> validate_monitors(const monitoring::MonitorsDocument& document, const Model* pt, const Model* dt,
                                          const json::Json& telemetry_schema,
                                          const std::optional<std::string>& ontology_text) {
    std::vector<Diagnostic> out;
    for (const monitoring::Finding& f : monitoring::validate_document(document)) {
        out.push_back(Diagnostic{f.severity, f.code, f.message, "", {"document", f.path, ""}, std::nullopt});
    }
    std::set<std::string> pt_labels;
    if (pt != nullptr) {
        for (const EdgeDecl& e : pt->edges) {
            if (e.sync && e.sync->direction == '!') pt_labels.insert(edge_label(e));
        }
    }
    std::set<std::string> fields;
    if (telemetry_schema.is_array()) {
        for (const json::Json& f : telemetry_schema) {
            if (f.is_object() && f.contains("id") && f.at("id").is_string()) fields.insert(f.at("id").get<std::string>());
        }
    }
    for (std::size_t i = 0; i < document.monitors.size(); ++i) {
        const monitoring::MonitorSpec& m = document.monitors[i];
        const std::string path = "monitors[" + std::to_string(i) + "]";
        if (m.kind == "conformance" && pt != nullptr && m.config.contains("events") && m.config.at("events").is_array()) {
            for (const json::Json& l : m.config.at("events")) {
                if (l.is_string() && !pt_labels.contains(l.get<std::string>())) {
                    out.push_back(Diagnostic{"error", "TWN012", "'" + l.get<std::string>() + "' is not an event of the PT view", "",
                                             {"document", path + ".events", ""}, std::nullopt});
                }
            }
        }
        if (m.kind == "property" && dt != nullptr && m.config.contains("property") && m.config.at("property").is_string()) {
            const PropertyAnalysis a = analyse_property(m.config.at("property").get<std::string>(), *dt, ontology_text);
            for (Diagnostic d : a.diagnostics) {
                if (d.code == "TWP001") continue;  // already reported as TWN011
                d.element = {"document", path + ".property", ""};
                out.push_back(std::move(d));
            }
        }
        if (m.kind == "data_quality" && telemetry_schema.is_array() && m.config.contains("field") &&
            m.config.at("field").is_string() && !fields.contains(m.config.at("field").get<std::string>())) {
            out.push_back(Diagnostic{"error", "TWN021",
                                     "'" + m.config.at("field").get<std::string>() + "' is not a telemetry field of the twin type",
                                     "", {"document", path + ".field", ""}, std::nullopt});
        }
    }
    return out;
}

}  // namespace twin::authoring
