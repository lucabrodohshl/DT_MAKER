/**
 * @file compiler.cpp
 * @brief Compiler orchestration: parse, strict read, translation validation, emit IR.
 */
#include "twin/compiler/compiler.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "crosscheck.hpp"
#include "interpretation.hpp"
#include "twin/core/sha256.hpp"
#include "twin/core/version.hpp"
#include "twin/core/wall_clock.hpp"
#include "twin/ir/codec.hpp"
#include "twin/ir/validate.hpp"
#include "utap/utap.hpp"
#include "utap_reader.hpp"

namespace twin::compiler {
namespace {

Diagnostic err(std::string code, std::string where, std::string message, std::string hint = {}) {
    return Diagnostic{Severity::Error, std::move(code), std::move(message), std::move(where),
                      std::move(hint)};
}

/// Transition ids "<source>.<label>.<target>" with "#k" for the k-th repetition.
std::vector<std::string> transition_ids(const detail::SourceModel& m) {
    std::vector<std::string> ids;
    std::map<std::string, int> seen;
    for (const detail::SourceEdge& e : m.edges) {
        const std::string base = m.locations[e.source].name + "." + e.action.label() + "." +
                                 m.locations[e.target].name;
        const int n = seen[base]++;
        ids.push_back(n == 0 ? base : base + "#" + std::to_string(n));
    }
    return ids;
}

ir::Conjunction conjunction(const std::vector<detail::SourceAtom>& atoms) {
    ir::Conjunction g;
    for (const detail::SourceAtom& a : atoms) {
        g.push_back(a.constraint);
    }
    ir::canonicalize(g);
    return g;
}

}  // namespace

const char* to_string(Severity severity) noexcept {
    switch (severity) {
        case Severity::Error: return "error";
        case Severity::Warning: return "warning";
        case Severity::Note: return "note";
    }
    return "error";
}

std::string render(const Diagnostic& d) {
    std::string out = std::string(to_string(d.severity)) + "[" + d.code + "] " + d.where + ": " + d.message;
    if (!d.hint.empty()) {
        out += "\n    hint: " + d.hint;
    }
    return out;
}

std::int64_t max_clock_bound() noexcept { return detail::max_constraint_constant(); }

std::recursive_mutex& utap_mutex() noexcept {
    static std::recursive_mutex m;
    return m;
}

bool has_errors(const std::vector<Diagnostic>& diagnostics) noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const Diagnostic& d) { return d.severity == Severity::Error; });
}

json::Json to_json(const CompilationManifest& m) {
    json::Json diags = json::Json::array();
    for (const Diagnostic& d : m.diagnostics) {
        diags.push_back(json::Json{{"severity", to_string(d.severity)},
                                   {"code", d.code},
                                   {"message", d.message},
                                   {"where", d.where},
                                   {"hint", d.hint}});
    }
    json::Json overlapping = json::Json::array();
    for (const auto& [a, b] : m.determinism.overlapping) {
        overlapping.push_back(json::Json::array({a, b}));
    }
    return json::Json{
        {"format", "twin-compilation-manifest/1"},
        {"source", {{"path", m.source_path}, {"sha256", m.source_sha256}}},
        {"interpretation_sha256", m.interpretation_sha256},
        {"ir", {{"format", m.ir_format}, {"sha256", m.ir_sha256}}},
        {"compiler_version", m.compiler_version},
        {"compiled_at", m.compiled_at},
        {"model", {{"id", m.model_id}, {"version", m.model_version}, {"template", m.source_template}}},
        {"translation_validation",
         {{"passed", m.translation_validation.passed},
          {"checks", static_cast<std::int64_t>(m.translation_validation.checks)},
          {"mismatches", m.translation_validation.mismatches}}},
        {"determinism",
         {{"event_deterministic", m.determinism.event_deterministic}, {"overlapping", overlapping}}},
        {"diagnostics", diags}};
}

std::variant<CompileResult, CompileFailure> compile_file(const std::filesystem::path& source,
                                                         const CompileOptions& options) {
    const std::lock_guard<std::recursive_mutex> utap_lock(utap_mutex());  // UTAP is not thread-safe
    std::vector<Diagnostic> diags;
    auto fail = [&diags]() { return CompileFailure{std::move(diags)}; };

    // ---- source bytes (hashed for provenance) -----------------------------------
    std::ifstream in(source, std::ios::binary);
    if (!in) {
        diags.push_back(err("TWC000", source.string(), "cannot open source document"));
        return fail();
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string source_sha = sha256_hex(buffer.str());

    if (options.model_id.empty()) {
        diags.push_back(err("TWC080", "options", "a model id is required"));
    }
    if (Status s = validate_time_base(TimeBase{options.ticks_per_unit}); !s) {
        diags.push_back(err("TWC081", "options", s.error().message));
    }

    // ---- 1. parse with UTAP (the aligner's parser) ---------------------------------
    UTAP::Document doc;
    if (parse_XML_file(source, doc, true) != 0) {
        diags.push_back(err("TWC000", source.string(), "UTAP cannot parse the document"));
        return fail();
    }

    // ---- 2. strict extraction -----------------------------------------------------
    std::optional<detail::SourceModel> sm =
        detail::read_strict(doc, detail::ReaderOptions{options.legacy_system_declaration}, diags);
    std::optional<detail::InterpretationEntries> interp;
    if (options.interpretation) {
        interp = detail::read_interpretation(*options.interpretation, diags);
    }
    if (!sm || has_errors(diags)) {
        return fail();
    }

    // ---- 3. translation validation against the aligner ----------------------------
    TranslationValidation tv = detail::crosscheck_with_aligner(source, *sm);
    if (!tv.passed) {
        for (const std::string& mismatch : tv.mismatches) {
            diags.push_back(err("TWC060", "translation validation", mismatch,
                                "the compiler and the semantic aligner read this model differently; "
                                "the IR would not be the verified model"));
        }
        return fail();
    }

    // ---- 4. emit IR ---------------------------------------------------------------
    const std::vector<std::string> ids = transition_ids(*sm);
    ir::Model m;
    m.info = ir::ModelInfo{options.model_id, options.model_version, sm->template_name, source_sha};
    m.time = TimeBase{options.ticks_per_unit};
    m.clocks = sm->clocks;
    m.channels = sm->channels;
    std::sort(m.channels.begin(), m.channels.end());
    for (const detail::SourceLocation& l : sm->locations) {
        m.locations.push_back(ir::Location{l.name, conjunction(l.invariant), std::nullopt});
    }
    m.initial = static_cast<ir::LocationIndex>(sm->initial);
    std::set<std::string> labels;
    for (std::size_t k = 0; k < sm->edges.size(); ++k) {
        const detail::SourceEdge& e = sm->edges[k];
        std::vector<ir::ClockIndex> resets = e.resets;
        std::sort(resets.begin(), resets.end());
        resets.erase(std::unique(resets.begin(), resets.end()), resets.end());
        m.transitions.push_back(ir::Transition{ids[k], static_cast<ir::LocationIndex>(e.source),
                                               static_cast<ir::LocationIndex>(e.target), e.action,
                                               conjunction(e.guard), std::move(resets)});
        if (e.action.kind != ir::ActionKind::Internal) {
            labels.insert(e.action.label());
        }
    }
    for (std::size_t i = 0; i < m.locations.size(); ++i) {
        std::string formula;
        if (interp) {
            const auto it = interp->locations.find(m.locations[i].id);
            if (it != interp->locations.end()) formula = it->second;
        }
        m.propositions.push_back(ir::Proposition{"at(" + m.locations[i].id + ")",
                                                 static_cast<ir::LocationIndex>(i), formula});
    }
    if (interp) {
        for (const auto& [name, formula] : interp->locations) {
            if (!ir::find_location(m, name)) {
                diags.push_back(Diagnostic{Severity::Warning, "TWC074",
                                           "interpretation of unknown location '" + name + "' is ignored",
                                           options.interpretation->filename().string(), {}});
            }
        }
        for (const auto& [label, formula] : interp->events) {
            if (labels.count(label) == 0) {
                diags.push_back(Diagnostic{Severity::Warning, "TWC075",
                                           "interpretation of label '" + label + "', which no transition carries",
                                           options.interpretation->filename().string(), {}});
                continue;
            }
            m.event_interpretations.push_back(ir::EventInterpretation{label, formula});
        }
        for (const std::string& label : labels) {
            if (interp->events.count(label) == 0) {
                diags.push_back(Diagnostic{
                    Severity::Warning, "TWC076", "label '" + label + "' has no interpretation",
                    options.interpretation->filename().string(),
                    "the aligner neither matches nor explores uninterpreted synchronised labels"});
            }
        }
    }
    if (Status s = ir::validate(m); !s) {
        diags.push_back(err("TWC090", "IR emission", "emitted IR is not well-formed (compiler bug): " +
                                                         s.error().to_string()));
        return fail();
    }
    Result<std::string> canonical = ir::to_canonical_text(m);
    if (!canonical) {
        diags.push_back(err("TWC091", "IR emission", canonical.error().to_string()));
        return fail();
    }

    // ---- 5. analysis + manifest -------------------------------------------------------
    CompilationManifest manifest;
    manifest.source_path = source.string();
    manifest.source_sha256 = source_sha;
    manifest.interpretation_sha256 = interp ? interp->sha256 : std::string();
    manifest.ir_sha256 = sha256_hex(canonical.value());
    manifest.compiler_version = std::string(version::kCompiler);
    manifest.ir_format = std::string(version::kIrFormat);
    manifest.compiled_at = build_timestamp_utc();
    manifest.model_id = m.info.id;
    manifest.model_version = m.info.version;
    manifest.source_template = m.info.source_template;
    manifest.translation_validation = std::move(tv);
    manifest.determinism = detail::analyse_determinism(*sm, ids);
    manifest.diagnostics = std::move(diags);
    return CompileResult{std::move(m), std::move(canonical).value(), std::move(manifest)};
}

}  // namespace twin::compiler
