/**
 * @file alignment.cpp
 * @brief Runs the unmodified SemPTDTAlignmentICSE checker and records evidence.
 */
#include "twin/alignment/alignment.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

#include "dtpta/domain_parser.h"
#include "dtpta/interpretation.h"
#include "dtpta/semantic_checker.h"
#include "dtpta/timedautomaton.h"
#include "capture.hpp"
#include "twin/alignment/aligner_identity.hpp"
#include "twin/alignment/diagnostics.hpp"
#include "twin/compiler/compiler.hpp"
#include "twin/core/sha256.hpp"

namespace twin::alignment {
namespace {

using detail::CoutCapture;

Result<std::string> file_sha256(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return make_error(ErrorCode::IoError, "cannot read input file").with("file", path.string());
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return sha256_hex(buf.str());
}

/// Synchronised labels of an automaton as the aligner spells them ("a!" / "a?").
std::set<std::string> sync_labels(const dtpta::TimedAutomaton& ta) {
    std::set<std::string> out;
    for (const dtpta::Transition& t : ta.get_transitions()) {
        if (t.has_synchronization()) {
            out.insert(t.channel + (t.is_sender ? "!" : "?"));
        }
    }
    return out;
}

void lint_labels(const std::string& view, const dtpta::TimedAutomaton& ta,
                 const dtpta::InterpretationMap& imap, dtpta::Ontology& ontology,
                 std::vector<LintFinding>& out) {
    const std::set<std::string> labels = sync_labels(ta);
    for (const std::string& label : labels) {
        if (label.back() == '?') {
            out.push_back({"error", "TWA011",
                           view + " label '" + label +
                               "' is a receive label: the .interp format cannot interpret it, so the "
                               "aligner neither matches nor explores its transitions"});
        } else if (!imap.has_event(label)) {
            out.push_back({"error", "TWA010",
                           view + " label '" + label +
                               "' has no interpretation: the aligner skips such synchronised "
                               "transitions (they are neither matched nor treated as tau)"});
        } else if (ontology.entails(*imap.get_event(label))) {
            out.push_back({"error", "TWA012",
                           view + " label '" + label +
                               "' has a tautological interpretation: the aligner removes it from "
                               "dom(I) and then skips its transitions"});
        }
    }
    for (const std::string& label : imap.event_labels()) {
        if (labels.count(label) == 0) {
            out.push_back({"warning", "TWA020",
                           view + " interpretation of '" + label + "', which no transition carries"});
        }
    }
}

}  // namespace

Result<AlignmentEvidence> check_alignment(const AlignmentInputs& in) {
    AlignmentEvidence ev;
    ev.inputs = json::Json::object();
    const std::pair<const char*, const std::filesystem::path*> files[] = {
        {"pt_model", &in.pt_model},
        {"dt_model", &in.dt_model},
        {"ontology", &in.ontology},
        {"pt_interpretation", &in.pt_interpretation},
        {"dt_interpretation", &in.dt_interpretation}};
    for (const auto& [role, path] : files) {
        Result<std::string> h = file_sha256(*path);
        if (!h) return std::move(h).error();
        ev.inputs[role] = json::Json{{"file", path->filename().string()}, {"sha256", h.value()}};
    }

    // 1. Both views must be inside the fragment the aligner reads faithfully.
    for (const auto& [view, path, interp] :
         {std::tuple{"PT", &in.pt_model, &in.pt_interpretation},
          std::tuple{"DT", &in.dt_model, &in.dt_interpretation}}) {
        compiler::CompileOptions opts;
        opts.model_id = std::string(view) == "PT" ? "pt-view" : "dt-view";
        opts.interpretation = *interp;
        opts.legacy_system_declaration = in.legacy_system_declaration;
        auto outcome = compiler::compile_file(*path, opts);
        if (auto* failure = std::get_if<compiler::CompileFailure>(&outcome)) {
            for (const compiler::Diagnostic& d : failure->diagnostics) {
                if (d.severity == compiler::Severity::Error) {
                    ev.lint.push_back({"error", "TWA001",
                                       std::string(view) + " view is outside the supported fragment: " +
                                           compiler::render(d)});
                }
            }
        } else {
            const auto& r = std::get<compiler::CompileResult>(outcome);
            (std::string(view) == "PT" ? ev.pt_ir_sha256 : ev.dt_ir_sha256) = r.manifest.ir_sha256;
        }
    }

    // 2. Run the aligner (Algorithm 1) exactly as its own drivers do.
    try {
        CoutCapture capture;
        std::unique_lock<std::recursive_mutex> utap_lock(compiler::utap_mutex());  // TA parsing uses UTAP
        dtpta::TimedAutomaton pt(in.pt_model.string());
        dtpta::TimedAutomaton dt(in.dt_model.string());
        utap_lock.unlock();
        pt.construct_zone_graph();
        dt.construct_zone_graph();
        ev.pt_zones = pt.get_num_states();
        ev.dt_zones = dt.get_num_states();
        const auto internal = [](const dtpta::TimedAutomaton& ta) {
            const auto& ts = ta.get_transitions();
            return static_cast<std::size_t>(std::count_if(ts.begin(), ts.end(), [](const dtpta::Transition& t) {
                return !t.has_synchronization();
            }));
        };
        ev.pt_internal_transitions = internal(pt);
        ev.dt_internal_transitions = internal(dt);

        dtpta::OntFileParser ont_parser;
        dtpta::InterpFileParser interp_parser;
        std::shared_ptr<dtpta::Ontology> ontology = ont_parser.parse(in.ontology.string());
        dtpta::DomainKnowledge dk(ontology);
        dk.pt_interp = interp_parser.parse(in.pt_interpretation.string(), ontology);
        dk.dt_interp = interp_parser.parse(in.dt_interpretation.string(), ontology);

        dtpta::SemanticAlignmentChecker checker;
        dtpta::SemanticAlignmentResult result;
        ev.aligned = checker.check_semantic_alignment(pt, dt, dk, result);
        ev.label_pairs = result.label_pairs_in_E;
        ev.final_relation_size = result.final_relation_size;
        ev.smt_calls = result.smt_calls_total;
        ev.fixpoint_iterations = result.fixpoint_iterations;
        if (result.counterexample_labels) {
            ev.counterexample_pt = result.counterexample_labels->first;
            ev.counterexample_dt = result.counterexample_labels->second;
        }

        // Syntactic baseline (classical weak timed bisimulation), on fresh automata.
        utap_lock.lock();
        dtpta::TimedAutomaton pt2(in.pt_model.string());
        dtpta::TimedAutomaton dt2(in.dt_model.string());
        utap_lock.unlock();
        dtpta::SemanticAlignmentChecker baseline;
        dtpta::SemanticAlignmentResult baseline_result;
        ev.syntactic_baseline_aligned = baseline.check_weak_timed_bisimulation(pt2, dt2, baseline_result);

        // 3. Lint and tables (after the verdict, so the SMT call count above is the aligner's).
        lint_labels("PT", pt, dk.pt_interp, *ontology, ev.lint);
        lint_labels("DT", dt, dk.dt_interp, *ontology, ev.lint);
        std::vector<std::string> pt_events = dk.pt_interp.event_labels();
        std::vector<std::string> dt_events = dk.dt_interp.event_labels();
        std::sort(pt_events.begin(), pt_events.end());
        std::sort(dt_events.begin(), dt_events.end());
        for (const std::string& a : pt_events) {
            LabelEquivalence row{a, {}};
            for (const std::string& b : dt_events) {
                if (ontology->entails_iff(*dk.pt_interp.get_event(a), *dk.dt_interp.get_event(b))) {
                    row.dt_labels.push_back(b);
                }
            }
            ev.label_equivalence.push_back(std::move(row));
        }
        std::vector<std::string> pt_locs = dk.pt_interp.state_names();
        std::vector<std::string> dt_locs = dk.dt_interp.state_names();
        std::sort(pt_locs.begin(), pt_locs.end());
        std::sort(dt_locs.begin(), dt_locs.end());
        for (const std::string& d : dt_locs) {
            LocationCorrespondence row{d, {}};
            for (const std::string& p : pt_locs) {
                if (ontology->entails_iff(*dk.pt_interp.get_state(p), *dk.dt_interp.get_state(d))) {
                    row.pt_equivalents.push_back(p);
                }
            }
            ev.location_consistency.push_back(std::move(row));
        }
    } catch (const std::exception& e) {
        return make_error(ErrorCode::ParseError, "the aligner could not process the inputs")
            .with("detail", e.what());
    }
    ev.lint_clean = std::none_of(ev.lint.begin(), ev.lint.end(),
                                 [](const LintFinding& f) { return f.severity == "error"; });
    return ev;
}

json::Json to_json(const AlignmentEvidence& ev) {
    json::Json lint = json::Json::array();
    for (const LintFinding& f : ev.lint) {
        lint.push_back(json::Json{{"severity", f.severity}, {"code", f.code}, {"message", f.message}});
    }
    json::Json e = json::Json::array();
    for (const LabelEquivalence& row : ev.label_equivalence) {
        e.push_back(json::Json{{"pt", row.pt_label}, {"dt", row.dt_labels}});
    }
    json::Json loc = json::Json::array();
    for (const LocationCorrespondence& row : ev.location_consistency) {
        loc.push_back(json::Json{{"dt", row.dt_location}, {"pt_equivalents", row.pt_equivalents}});
    }
    auto n = [](std::size_t v) { return static_cast<std::int64_t>(v); };
    return json::Json{
        {"format", "twin-alignment-evidence/1"},
        {"aligner",
         {{"name", std::string(kAlignerName)},
          {"source_digest", std::string(kAlignerSourceDigest)},
          {"source_files", kAlignerSourceFiles},
          {"procedure", "dtpta::SemanticAlignmentChecker::check_semantic_alignment"}}},
        {"inputs", ev.inputs},
        {"compiled_views", {{"pt_ir_sha256", ev.pt_ir_sha256}, {"dt_ir_sha256", ev.dt_ir_sha256}}},
        {"verdict",
         {{"aligned", ev.aligned},
          {"counterexample", {{"pt", ev.counterexample_pt}, {"dt", ev.counterexample_dt}}},
          {"label_pairs", n(ev.label_pairs)},
          {"final_relation_size", n(ev.final_relation_size)},
          {"smt_calls", n(ev.smt_calls)},
          {"fixpoint_iterations", n(ev.fixpoint_iterations)},
          {"pt_zones", n(ev.pt_zones)},
          {"dt_zones", n(ev.dt_zones)}}},
        {"syntactic_baseline", {{"aligned", ev.syntactic_baseline_aligned}}},
        {"modes", alignment_modes(ev.aligned, ev.pt_internal_transitions, ev.dt_internal_transitions)},
        {"internal_transitions", {{"pt", n(ev.pt_internal_transitions)}, {"dt", n(ev.dt_internal_transitions)}}},
        {"label_equivalence", e},
        {"location_consistency", loc},
        {"lint", {{"clean", ev.lint_clean}, {"findings", lint}}},
        {"scope_notes",
         json::Json::array({"Verdict of Algorithm 1 as implemented: Conditions II/III over zone-graph "
                            "weak successors from the initial zone pair.",
                            "Condition I is not evaluated by the aligner; location_consistency is "
                            "informational.",
                            "See docs/existing-aligner-integration.md for the aligner's scope."})}};
}

}  // namespace twin::alignment
