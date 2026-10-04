/**
 * @file builder.cpp
 * @brief Release pipeline: compile, align, bundle, hash, self-verify.
 */
#include "twin/package/builder.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

#include "twin/alignment/alignment.hpp"
#include "twin/alignment/aligner_identity.hpp"
#include "twin/compiler/compiler.hpp"
#include "twin/core/sha256.hpp"
#include "twin/core/version.hpp"
#include "twin/core/wall_clock.hpp"

namespace twin::package {
namespace {

Result<std::string> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return make_error(ErrorCode::IoError, "cannot read file").with("file", path.string());
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

Status write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
    out.close();
    if (!out) {
        return make_error(ErrorCode::IoError, "cannot write file").with("file", path.string());
    }
    return ok_status();
}

}  // namespace

Result<BuildResult> build_package(const BuildInputs& in, const std::filesystem::path& out_dir) {
    // ---- 1. compile V_D ----------------------------------------------------------------
    compiler::CompileOptions copts;
    copts.model_id = in.model_id;
    copts.model_version = in.model_version;
    copts.ticks_per_unit = in.ticks_per_unit;
    copts.interpretation = in.dt_interpretation;
    copts.legacy_system_declaration = in.legacy_system_declaration;
    auto outcome = compiler::compile_file(in.dt_model, copts);
    if (auto* failure = std::get_if<compiler::CompileFailure>(&outcome)) {
        Error e = make_error(ErrorCode::ValidationError, "the DT view does not compile");
        for (const compiler::Diagnostic& d : failure->diagnostics) {
            if (d.severity == compiler::Severity::Error) e.with("diagnostic", compiler::render(d));
        }
        return e;
    }
    compiler::CompileResult compiled = std::get<compiler::CompileResult>(std::move(outcome));

    // ---- 2. alignment evidence ------------------------------------------------------------
    Result<alignment::AlignmentEvidence> evidence = alignment::check_alignment(
        alignment::AlignmentInputs{in.pt_model, in.dt_model, in.ontology, in.pt_interpretation,
                                   in.dt_interpretation, in.legacy_system_declaration});
    if (!evidence) {
        return std::move(evidence).error();
    }
    const alignment::AlignmentEvidence& ev = evidence.value();
    if (!in.allow_unaligned && (!ev.aligned || !ev.lint_clean)) {
        Error e = make_error(ErrorCode::ValidationError,
                             ev.aligned ? "alignment evidence has lint errors"
                                        : "the aligner did not establish V_P ~Phi V_D");
        for (const alignment::LintFinding& f : ev.lint) {
            if (f.severity == "error") e.with("lint", f.code + " " + f.message);
        }
        return e;
    }

    // ---- 3. output directory ----------------------------------------------------------------
    std::error_code ec;
    if (std::filesystem::exists(out_dir, ec) && !std::filesystem::is_empty(out_dir, ec)) {
        if (!in.overwrite) {
            return make_error(ErrorCode::InvalidArgument, "output directory exists and is not empty")
                .with("directory", out_dir.string());
        }
        std::filesystem::remove_all(out_dir, ec);
    }
    std::filesystem::create_directories(out_dir, ec);

    // ---- 4. artefacts ---------------------------------------------------------------------------
    struct Artefact {
        std::string path;
        std::string role;
        std::string bytes;
    };
    std::vector<Artefact> artefacts;
    const std::pair<const char*, const std::filesystem::path*> copies[] = {
        {"source", &in.dt_model},           {"dt_interpretation", &in.dt_interpretation},
        {"ontology", &in.ontology},         {"pt_model", &in.pt_model},
        {"pt_interpretation", &in.pt_interpretation}};
    const std::map<std::string, std::string> target_path = {
        {"source", "model/dt_view.xml"},          {"dt_interpretation", "model/dt.interp"},
        {"ontology", "semantics/domain.ont"},     {"pt_model", "semantics/pt_view.xml"},
        {"pt_interpretation", "semantics/pt.interp"}};
    for (const auto& [role, src] : copies) {
        Result<std::string> bytes = read_bytes(*src);
        if (!bytes) return std::move(bytes).error();
        artefacts.push_back(Artefact{target_path.at(role), role, std::move(bytes).value()});
    }
    artefacts.push_back(Artefact{"ir/model.ir.json", "ir", compiled.canonical_ir});
    Result<std::string> comp_text = json::canonical_dump(compiler::to_json(compiled.manifest));
    Result<std::string> ev_text = json::canonical_dump(alignment::to_json(ev));
    if (!comp_text) return std::move(comp_text).error();
    if (!ev_text) return std::move(ev_text).error();
    artefacts.push_back(Artefact{"evidence/compilation.json", "compilation_manifest", comp_text.value()});
    artefacts.push_back(Artefact{"evidence/alignment.json", "alignment_evidence", ev_text.value()});

    Manifest m;
    m.format = std::string(version::kPackageFormat);
    m.model_id = in.model_id;
    m.model_version = in.model_version;
    m.kernel_compat = std::string(version::kKernelCompat);
    m.compiler_version = std::string(version::kCompiler);
    m.ir_format = std::string(version::kIrFormat);
    m.aligner_digest = std::string(alignment::kAlignerSourceDigest);
    m.created_at = build_timestamp_utc();
    m.aligned = ev.aligned;
    m.lint_clean = ev.lint_clean;
    m.translation_validated = compiled.manifest.translation_validation.passed;
    m.event_deterministic = compiled.manifest.determinism.event_deterministic;
    for (const Artefact& a : artefacts) {
        if (Status s = write_bytes(out_dir / a.path, a.bytes); !s) return s.error();
        m.files.push_back(PackageFile{a.path, a.role, sha256_hex(a.bytes),
                                      static_cast<std::int64_t>(a.bytes.size())});
    }
    std::sort(m.files.begin(), m.files.end(),
              [](const PackageFile& a, const PackageFile& b) { return a.path < b.path; });
    Result<std::string> manifest_text = json::canonical_dump(to_json(m));
    if (!manifest_text) return std::move(manifest_text).error();
    if (Status s = write_bytes(out_dir / "manifest.json", manifest_text.value()); !s) return s.error();

    // ---- 5. self-verification with the runtime's loader --------------------------------------------
    Result<LoadedPackage> loaded = load_and_verify(out_dir, VerifyOptions{in.allow_unaligned});
    if (!loaded) {
        return std::move(loaded).error().with("stage", "self-verification after build");
    }
    return BuildResult{std::move(loaded).value(), compiled.manifest.diagnostics};
}

}  // namespace twin::package
