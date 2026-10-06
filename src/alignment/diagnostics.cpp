/**
 * @file diagnostics.cpp
 * @brief Alignment findings (categories + element links), modes, and label-pair explanations.
 */
#include "twin/alignment/diagnostics.hpp"

#include <map>
#include <regex>
#include <set>

#include "capture.hpp"
#include "dtpta/domain_parser.h"
#include "dtpta/interpretation.h"
#include "twin/compiler/reader.hpp"

namespace twin::alignment {
namespace {

/// Edges of one view, grouped by label ("a!", "a?", "tau").
struct ViewEdges {
    bool readable{false};
    std::map<std::string, std::vector<ElementLink>> by_label;
};

ViewEdges view_edges(std::string_view xml, const std::string& view) {
    ViewEdges out;
    const compiler::ReadResult r = compiler::read_uppaal(xml, compiler::ReaderOptions{true});
    if (!r.model) return out;
    out.readable = true;
    const compiler::SourceModel& m = *r.model;
    for (std::size_t k = 0; k < m.edges.size(); ++k) {
        const compiler::SourceEdge& e = m.edges[k];
        const std::string label = e.action.label();
        out.by_label[label].push_back(ElementLink{
            view, "edge", m.locations.at(e.source).name + " -" + label + "-> " + m.locations.at(e.target).name, k});
    }
    return out;
}

/// Links to every edge carrying @p label, plus its interpretation entry.
std::vector<ElementLink> label_links(const ViewEdges& edges, const std::string& view, const std::string& label) {
    std::vector<ElementLink> links;
    if (const auto it = edges.by_label.find(label); it != edges.by_label.end()) links = it->second;
    links.push_back(ElementLink{view + "_interpretation", "entry", label, std::nullopt});
    return links;
}

std::string str(const json::Json& j, std::initializer_list<const char*> path) {
    const json::Json* cur = &j;
    for (const char* k : path) {
        if (!cur->is_object() || !cur->contains(k)) return {};
        cur = &cur->at(k);
    }
    return cur->is_string() ? cur->get<std::string>() : std::string();
}

/// The parsed parts of an evidence document that diagnose() needs.
struct Evidence {
    bool aligned{false};
    std::string ce_pt;
    std::string ce_dt;
    std::map<std::string, std::vector<std::string>> e;  // PT label -> equivalent DT labels
    std::set<std::string> dt_in_e;
    std::vector<std::pair<std::string, std::vector<std::string>>> locations;  // DT location -> PT equivalents
    std::vector<std::pair<std::string, std::string>> lint;                    // code, message
};

Evidence read_evidence(const json::Json& ev) {
    Evidence out;
    if (ev.contains("verdict") && ev.at("verdict").is_object()) {
        out.aligned = ev.at("verdict").value("aligned", false);
    }
    out.ce_pt = str(ev, {"verdict", "counterexample", "pt"});
    out.ce_dt = str(ev, {"verdict", "counterexample", "dt"});
    if (ev.contains("label_equivalence") && ev.at("label_equivalence").is_array()) {
        for (const json::Json& row : ev.at("label_equivalence")) {
            std::vector<std::string> dts = row.value("dt", std::vector<std::string>{});
            out.dt_in_e.insert(dts.begin(), dts.end());
            out.e[row.value("pt", std::string())] = std::move(dts);
        }
    }
    if (ev.contains("location_consistency") && ev.at("location_consistency").is_array()) {
        for (const json::Json& row : ev.at("location_consistency")) {
            out.locations.emplace_back(row.value("dt", std::string()),
                                       row.value("pt_equivalents", std::vector<std::string>{}));
        }
    }
    if (ev.contains("lint") && ev.at("lint").is_object() && ev.at("lint").contains("findings")) {
        for (const json::Json& f : ev.at("lint").at("findings")) {
            out.lint.emplace_back(f.value("code", std::string()), f.value("message", std::string()));
        }
    }
    return out;
}

class Diagnoser {
public:
    Diagnoser(const json::Json& evidence, std::string_view pt_xml, std::string_view dt_xml)
        : ev_(read_evidence(evidence)), pt_(view_edges(pt_xml, "pt")), dt_(view_edges(dt_xml, "dt")) {}

    std::vector<AlignmentDiagnostic> run() {
        lint();
        unmatched();
        counterexample();
        states();
        return std::move(out_);
    }

private:
    void add(std::string category, std::string severity, std::string message, std::vector<ElementLink> links) {
        out_.push_back(AlignmentDiagnostic{std::move(category), std::move(severity), std::move(message), std::move(links)});
    }

    [[nodiscard]] const ViewEdges& edges(const std::string& view) const { return view == "pt" ? pt_ : dt_; }

    void lint() {
        static const std::regex label_finding(R"(^(PT|DT) (?:label|interpretation of) '([^']+)')");
        static const std::map<std::string, std::pair<std::string, std::string>> categories = {
            {"TWA001", {"outside_fragment", "error"}},
            {"TWA010", {"missing_interpretation", "error"}},
            {"TWA011", {"receive_label", "error"}},
            {"TWA012", {"tautological_interpretation", "error"}},
            {"TWA020", {"unused_interpretation", "warning"}}};
        for (const auto& [code, message] : ev_.lint) {
            const auto c = categories.find(code);
            if (c == categories.end()) continue;
            std::smatch m;
            std::vector<ElementLink> links;
            if (std::regex_search(message, m, label_finding)) {
                const std::string view = m[1].str() == "PT" ? "pt" : "dt";
                const std::string label = m[2].str();
                links = label_links(edges(view), view, label);
                flagged_[view].insert(label);
            } else if (message.starts_with("PT") || message.starts_with("DT")) {
                links.push_back(ElementLink{message.starts_with("PT") ? "pt" : "dt", "document", "", std::nullopt});
            }
            add(c->second.first, c->second.second, message, std::move(links));
        }
    }

    void unmatched() {
        const std::string severity = ev_.aligned ? "warning" : "error";
        for (const auto& [label, dts] : ev_.e) {
            if (!dts.empty() || flagged_["pt"].contains(label) || !pt_.by_label.contains(label)) continue;
            add("unmatched_pt_event", severity,
                "PT event '" + label + "' has no DT label with an equivalent meaning under the ontology "
                "(no b with Δ ⊨ I_P(" + label + ") ↔ I_D(b))",
                label_links(pt_, "pt", label));
        }
        for (const auto& [label, links] : dt_.by_label) {
            if (label == "tau" || label.back() != '!' || ev_.dt_in_e.contains(label) || flagged_["dt"].contains(label)) {
                continue;
            }
            add("unmatched_dt_event", severity,
                "DT event '" + label + "' has no PT label with an equivalent meaning under the ontology",
                label_links(dt_, "dt", label));
        }
    }

    [[nodiscard]] bool equivalent(const std::string& pt, const std::string& dt) const {
        const auto it = ev_.e.find(pt);
        return it != ev_.e.end() && std::find(it->second.begin(), it->second.end(), dt) != it->second.end();
    }

    [[nodiscard]] bool has_partner(const std::string& pt) const {
        const auto it = ev_.e.find(pt);
        return it != ev_.e.end() && !it->second.empty();
    }

    static std::string mismatch_message(const std::string& a, const std::string& b) {
        if (!a.empty() && !b.empty()) {
            return "PT '" + a + "' and DT '" + b +
                   "' mean the same under the ontology, but the views disagree on when (timing) or after which "
                   "steps (branching) they can occur";
        }
        if (!a.empty()) {
            return "PT '" + a + "' has equivalent DT labels, but V_D cannot match it at the time or in the state where "
                   "V_P performs it (timing or branching)";
        }
        if (!b.empty()) return "DT '" + b + "' cannot be matched by V_P at the time or in the state where V_D performs it";
        return "the views are not aligned: from the initial states the aligner finds no relation in which every step "
               "and delay of one view is matched by the other";
    }

    /// The aligner's counterexample uses "?", "<A-unmatched>" and "<B-unmatched>" for "no label".
    static std::string label_or_empty(const std::string& s) {
        return s.empty() || s == "?" || s.front() == '<' ? std::string() : s;
    }

    void counterexample() {
        if (ev_.aligned) return;
        const std::string a = label_or_empty(ev_.ce_pt);
        const std::string b = label_or_empty(ev_.ce_dt);
        std::vector<ElementLink> links;
        if (!a.empty()) links = label_links(pt_, "pt", a);
        if (!b.empty()) {
            std::vector<ElementLink> more = label_links(dt_, "dt", b);
            links.insert(links.end(), more.begin(), more.end());
        }
        if (!a.empty() && !b.empty() && !equivalent(a, b)) {
            add("ontology_equivalence", "error",
                "the aligner's first mismatch pairs PT '" + a + "' with DT '" + b +
                    "', whose interpretations are not equivalent under the ontology (see the pair explanation)",
                std::move(links));
            return;
        }
        if (!a.empty() && b.empty() && !has_partner(a)) return;          // already an unmatched_pt_event
        if (a.empty() && !b.empty() && !ev_.dt_in_e.contains(b)) return;  // already an unmatched_dt_event
        add("timing_or_branching", "error", mismatch_message(a, b), std::move(links));
    }

    void states() {
        for (const auto& [dt_location, pts] : ev_.locations) {
            if (!pts.empty()) continue;
            add("state_inconsistency", "info",
                "no PT location has a meaning equivalent to DT location '" + dt_location +
                    "' (Condition I, informational: the aligner does not require it)",
                {ElementLink{"dt", "location", dt_location, std::nullopt},
                 ElementLink{"dt_interpretation", "entry", dt_location, std::nullopt}});
        }
    }

    Evidence ev_;
    ViewEdges pt_;
    ViewEdges dt_;
    std::map<std::string, std::set<std::string>> flagged_;
    std::vector<AlignmentDiagnostic> out_;
};

}  // namespace

std::vector<AlignmentDiagnostic> diagnose(const json::Json& evidence, std::string_view pt_xml, std::string_view dt_xml) {
    return Diagnoser(evidence, pt_xml, dt_xml).run();
}

json::Json to_json(const AlignmentDiagnostic& d) {
    json::Json links = json::Json::array();
    for (const ElementLink& l : d.links) {
        json::Json j{{"view", l.view}, {"kind", l.kind}, {"name", l.name}};
        if (l.index) j["index"] = static_cast<std::int64_t>(*l.index);
        links.push_back(std::move(j));
    }
    return json::Json{{"category", d.category}, {"severity", d.severity}, {"message", d.message}, {"links", links}};
}

json::Json to_json(const std::vector<AlignmentDiagnostic>& ds) {
    json::Json out = json::Json::array();
    for (const AlignmentDiagnostic& d : ds) out.push_back(to_json(d));
    return out;
}

json::Json alignment_modes(bool aligned, std::size_t pt_internal, std::size_t dt_internal) {
    if (!aligned) {
        return json::Json{{"weak", "not_aligned"},
                          {"strong", "not_aligned"},
                          {"strong_reason", "strong alignment implies weak alignment, which does not hold"}};
    }
    if (pt_internal == 0 && dt_internal == 0) {
        return json::Json{{"weak", "aligned"},
                          {"strong", "aligned"},
                          {"strong_reason", "neither view has internal (tau) transitions, so weak and strong timed "
                                            "bisimulation coincide"}};
    }
    return json::Json{{"weak", "aligned"},
                      {"strong", "not_decidable"},
                      {"strong_reason", "V_P has " + std::to_string(pt_internal) + " and V_D has " +
                                            std::to_string(dt_internal) +
                                            " internal (tau) transitions; the checker decides weak alignment only"}};
}

Result<PairExplanation> explain_pair(const AlignmentInputs& in, std::string_view pt, std::string_view dt,
                                     std::string_view kind) {
    if (kind != "event" && kind != "location") {
        return make_error(ErrorCode::InvalidArgument, R"(kind must be "event" or "location")");
    }
    try {
        detail::CoutCapture capture;
        dtpta::OntFileParser ont_parser;
        dtpta::InterpFileParser interp_parser;
        std::shared_ptr<dtpta::Ontology> ontology = ont_parser.parse(in.ontology.string());
        const dtpta::InterpretationMap ip = interp_parser.parse(in.pt_interpretation.string(), ontology);
        const dtpta::InterpretationMap id = interp_parser.parse(in.dt_interpretation.string(), ontology);
        const bool event = kind == "event";
        std::optional<z3::expr> a = event ? ip.get_event(std::string(pt)) : ip.get_state(std::string(pt));
        std::optional<z3::expr> b = event ? id.get_event(std::string(dt)) : id.get_state(std::string(dt));
        if (!a) return make_error(ErrorCode::NotFound, "the PT interpretation does not interpret '" + std::string(pt) + "'");
        if (!b) return make_error(ErrorCode::NotFound, "the DT interpretation does not interpret '" + std::string(dt) + "'");
        PairExplanation out;
        out.pt_implies_dt = ontology->entails(z3::implies(*a, *b));
        out.dt_implies_pt = ontology->entails(z3::implies(*b, *a));
        out.equivalent = ontology->entails_iff(*a, *b);
        return out;
    } catch (const std::exception& e) {
        return make_error(ErrorCode::ParseError, "the aligner could not read the ontology or interpretations")
            .with("detail", e.what());
    }
}

json::Json to_json(const PairExplanation& p) {
    return json::Json{{"equivalent", p.equivalent}, {"ptImpliesDt", p.pt_implies_dt}, {"dtImpliesPt", p.dt_implies_pt}};
}

}  // namespace twin::alignment
