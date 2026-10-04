/**
 * @file package.cpp
 * @brief Manifest (de)serialisation and package verification.
 */
#include "twin/package/package.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>

#include "twin/core/sha256.hpp"
#include "twin/core/version.hpp"
#include "twin/ir/codec.hpp"

namespace twin::package {
namespace {

using json::Json;

constexpr std::array<const char*, 8> kRequiredRoles = {
    "source",   "dt_interpretation", "ir",       "ontology", "pt_model", "pt_interpretation",
    "alignment_evidence", "compilation_manifest"};

Result<std::string> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return make_error(ErrorCode::IoError, "cannot read file").with("file", path.string());
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

/// Relative, forward-slash path that cannot escape the package directory.
bool is_safe_relative_path(const std::string& p) {
    if (p.empty() || p.front() == '/' || p.find('\\') != std::string::npos ||
        p.find(':') != std::string::npos) {
        return false;
    }
    std::istringstream parts(p);
    std::string segment;
    while (std::getline(parts, segment, '/')) {
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
    }
    return true;
}

/// Accumulates checks; `ok()` is false after the first failed check.
class Verifier {
public:
    void check(std::string name, bool passed, std::string detail = {}) {
        checks_.push_back(Check{std::move(name), passed, std::move(detail)});
        failed_ = failed_ || !passed;
    }
    [[nodiscard]] bool ok() const noexcept { return !failed_; }
    std::vector<Check>& checks() noexcept { return checks_; }

private:
    std::vector<Check> checks_;
    bool failed_{false};
};

std::string str_at(const Json& j, std::initializer_list<const char*> path) {
    const Json* cur = &j;
    for (const char* key : path) {
        if (!cur->is_object() || !cur->contains(key)) return {};
        cur = &cur->at(key);
    }
    return cur->is_string() ? cur->get<std::string>() : std::string();
}

std::optional<bool> bool_at(const Json& j, std::initializer_list<const char*> path) {
    const Json* cur = &j;
    for (const char* key : path) {
        if (!cur->is_object() || !cur->contains(key)) return std::nullopt;
        cur = &cur->at(key);
    }
    if (!cur->is_boolean()) return std::nullopt;
    return cur->get<bool>();
}

struct Outcome {
    std::vector<Check> checks;
    std::optional<LoadedPackage> package;
};

Outcome verify_impl(const std::filesystem::path& dir, const VerifyOptions& options, bool stop_early) {
    Verifier v;
    LoadedPackage pkg;
    pkg.directory = dir;
    auto done = [&]() { return stop_early && !v.ok(); };
    auto finish = [&]() {
        Outcome out;
        out.checks = std::move(v.checks());
        if (v.ok()) {
            pkg.checks = out.checks;
            out.package = std::move(pkg);
        }
        return out;
    };

    // ---- manifest -------------------------------------------------------------
    Result<std::string> manifest_bytes = read_bytes(dir / "manifest.json");
    v.check("manifest.json readable", manifest_bytes.ok(),
            manifest_bytes.ok() ? "" : manifest_bytes.error().to_string());
    if (!manifest_bytes) return finish();
    Result<Json> manifest_json = json::parse_canonical(manifest_bytes.value());
    v.check("manifest.json is canonical JSON", manifest_json.ok(),
            manifest_json.ok() ? "" : manifest_json.error().to_string());
    if (!manifest_json) return finish();
    pkg.package_hash = sha256_hex(manifest_bytes.value());
    Result<Manifest> manifest = manifest_from_json(manifest_json.value());
    v.check("manifest structure", manifest.ok(), manifest.ok() ? "" : manifest.error().to_string());
    if (!manifest) return finish();
    pkg.manifest = manifest.value();
    const Manifest& m = pkg.manifest;
    v.check("package format", m.format == version::kPackageFormat,
            "found '" + m.format + "', supported '" + std::string(version::kPackageFormat) + "'");
    v.check("kernel compatibility", m.kernel_compat == version::kKernelCompat,
            "package requires '" + m.kernel_compat + "', this kernel is '" +
                std::string(version::kKernelCompat) + "'");
    if (done()) return finish();

    // ---- files ------------------------------------------------------------------
    std::map<std::string, const PackageFile*> by_role;
    std::map<std::string, std::string> bytes_by_role;
    for (const PackageFile& f : m.files) {
        const std::string name = "file " + f.path;
        if (!is_safe_relative_path(f.path)) {
            v.check(name + " path", false, "path escapes the package or is malformed");
            continue;
        }
        Result<std::string> bytes = read_bytes(dir / f.path);
        if (!bytes) {
            v.check(name + " present", false, bytes.error().to_string());
            continue;
        }
        const std::string actual = sha256_hex(bytes.value());
        v.check(name + " sha256", actual == f.sha256, "manifest " + f.sha256 + ", actual " + actual);
        v.check(name + " size", static_cast<std::int64_t>(bytes.value().size()) == f.size,
                "manifest " + std::to_string(f.size) + ", actual " + std::to_string(bytes.value().size()));
        if (!by_role.emplace(f.role, &f).second) {
            v.check("role " + f.role + " unique", false, "role listed more than once");
        }
        bytes_by_role[f.role] = std::move(bytes).value();
    }
    for (const char* role : kRequiredRoles) {
        v.check(std::string("role ") + role + " present", by_role.count(role) == 1);
    }
    if (done() || !v.ok()) return finish();
    auto sha = [&](const char* role) { return by_role.at(role)->sha256; };

    // ---- IR -------------------------------------------------------------------------
    Result<ir::Model> model = ir::from_canonical_text(bytes_by_role.at("ir"));
    v.check("IR decodes canonically and is well-formed", model.ok(),
            model.ok() ? "" : model.error().to_string());
    if (!model) return finish();
    pkg.model = std::move(model).value();
    pkg.ir_sha256 = sha("ir");
    pkg.source_sha256 = sha("source");
    v.check("IR model id matches manifest", pkg.model.info.id == m.model_id,
            "IR '" + pkg.model.info.id + "', manifest '" + m.model_id + "'");
    v.check("IR model version matches manifest", pkg.model.info.version == m.model_version,
            "IR '" + pkg.model.info.version + "', manifest '" + m.model_version + "'");
    v.check("IR is bound to the shipped source", pkg.model.info.source_sha256 == pkg.source_sha256,
            "IR records " + pkg.model.info.source_sha256 + ", source is " + pkg.source_sha256);
    if (done()) return finish();

    // ---- compilation manifest ---------------------------------------------------------
    Result<Json> comp = json::parse_canonical(bytes_by_role.at("compilation_manifest"));
    v.check("compilation manifest is canonical JSON", comp.ok(), comp.ok() ? "" : comp.error().to_string());
    if (!comp) return finish();
    const Json& cm = comp.value();
    v.check("compilation manifest IR hash", str_at(cm, {"ir", "sha256"}) == pkg.ir_sha256);
    v.check("compilation manifest source hash", str_at(cm, {"source", "sha256"}) == pkg.source_sha256);
    v.check("compilation manifest interpretation hash",
            str_at(cm, {"interpretation_sha256"}) == sha("dt_interpretation"));
    v.check("translation validation passed",
            bool_at(cm, {"translation_validation", "passed"}).value_or(false) && m.translation_validated);
    v.check("determinism flag consistent",
            bool_at(cm, {"determinism", "event_deterministic"}) == std::optional<bool>(m.event_deterministic));
    if (done()) return finish();

    // ---- alignment evidence -------------------------------------------------------------
    Result<Json> ev = json::parse_canonical(bytes_by_role.at("alignment_evidence"));
    v.check("alignment evidence is canonical JSON", ev.ok(), ev.ok() ? "" : ev.error().to_string());
    if (!ev) return finish();
    const Json& e = ev.value();
    v.check("alignment evidence format", str_at(e, {"format"}) == "twin-alignment-evidence/1");
    v.check("evidence bound to DT source", str_at(e, {"inputs", "dt_model", "sha256"}) == sha("source"));
    v.check("evidence bound to DT interpretation",
            str_at(e, {"inputs", "dt_interpretation", "sha256"}) == sha("dt_interpretation"));
    v.check("evidence bound to ontology", str_at(e, {"inputs", "ontology", "sha256"}) == sha("ontology"));
    v.check("evidence bound to PT view", str_at(e, {"inputs", "pt_model", "sha256"}) == sha("pt_model"));
    v.check("evidence bound to PT interpretation",
            str_at(e, {"inputs", "pt_interpretation", "sha256"}) == sha("pt_interpretation"));
    v.check("evidence aligner identity matches manifest",
            str_at(e, {"aligner", "source_digest"}) == m.aligner_digest);
    const bool aligned = bool_at(e, {"verdict", "aligned"}).value_or(false);
    const bool clean = bool_at(e, {"lint", "clean"}).value_or(false);
    v.check("manifest records the evidence verdict", aligned == m.aligned && clean == m.lint_clean);
    if (!options.allow_unaligned) {
        v.check("alignment verdict ALIGNED", aligned, "the aligner did not establish V_P ~Phi V_D");
        v.check("alignment lint clean", clean, "the evidence contains lint errors");
    }
    pkg.alignment_evidence = e;
    return finish();
}

}  // namespace

Json to_json(const Manifest& m) {
    Json files = Json::array();
    for (const PackageFile& f : m.files) {
        files.push_back(Json{{"path", f.path}, {"role", f.role}, {"sha256", f.sha256}, {"size", f.size}});
    }
    return Json{{"format", m.format},
                {"model", {{"id", m.model_id}, {"version", m.model_version}}},
                {"kernel_compat", m.kernel_compat},
                {"producers",
                 {{"compiler_version", m.compiler_version},
                  {"ir_format", m.ir_format},
                  {"aligner_source_digest", m.aligner_digest}}},
                {"created_at", m.created_at},
                {"verification",
                 {{"aligned", m.aligned},
                  {"lint_clean", m.lint_clean},
                  {"translation_validated", m.translation_validated},
                  {"event_deterministic", m.event_deterministic}}},
                {"files", files}};
}

Result<Manifest> manifest_from_json(const Json& doc) {
    if (Status s = json::expect_keys(doc, {"format", "model", "kernel_compat", "producers", "created_at",
                                           "verification", "files"});
        !s) {
        return s.error();
    }
    Manifest m;
    m.format = str_at(doc, {"format"});
    m.model_id = str_at(doc, {"model", "id"});
    m.model_version = str_at(doc, {"model", "version"});
    m.kernel_compat = str_at(doc, {"kernel_compat"});
    m.compiler_version = str_at(doc, {"producers", "compiler_version"});
    m.ir_format = str_at(doc, {"producers", "ir_format"});
    m.aligner_digest = str_at(doc, {"producers", "aligner_source_digest"});
    m.created_at = str_at(doc, {"created_at"});
    const std::optional<bool> aligned = bool_at(doc, {"verification", "aligned"});
    const std::optional<bool> clean = bool_at(doc, {"verification", "lint_clean"});
    const std::optional<bool> tv = bool_at(doc, {"verification", "translation_validated"});
    const std::optional<bool> det = bool_at(doc, {"verification", "event_deterministic"});
    if (!aligned || !clean || !tv || !det || m.model_id.empty() || m.format.empty()) {
        return make_error(ErrorCode::ValidationError, "incomplete package manifest");
    }
    m.aligned = *aligned;
    m.lint_clean = *clean;
    m.translation_validated = *tv;
    m.event_deterministic = *det;
    const Json& files = doc.at("files");
    if (!files.is_array()) {
        return make_error(ErrorCode::ValidationError, "manifest 'files' must be an array");
    }
    for (const Json& f : files) {
        if (Status s = json::expect_keys(f, {"path", "role", "sha256", "size"}); !s) return s.error();
        Result<std::int64_t> size = json::get_int(f, "size");
        if (!size) return std::move(size).error();
        PackageFile pf{str_at(f, {"path"}), str_at(f, {"role"}), str_at(f, {"sha256"}), size.value()};
        if (!is_canonical_hex_digest(pf.sha256)) {
            return make_error(ErrorCode::ValidationError, "malformed file digest").with("path", pf.path);
        }
        m.files.push_back(std::move(pf));
    }
    if (!std::is_sorted(m.files.begin(), m.files.end(),
                        [](const PackageFile& a, const PackageFile& b) { return a.path < b.path; })) {
        return make_error(ErrorCode::ValidationError, "manifest files must be sorted by path");
    }
    return m;
}

Result<LoadedPackage> load_and_verify(const std::filesystem::path& directory, const VerifyOptions& options) {
    Outcome out = verify_impl(directory, options, /*stop_early=*/true);
    if (out.package) {
        return std::move(*out.package);
    }
    const auto failed = std::find_if(out.checks.begin(), out.checks.end(),
                                     [](const Check& c) { return !c.passed; });
    Error e = make_error(ErrorCode::IntegrityError, "package verification failed");
    if (failed != out.checks.end()) {
        e.with("check", failed->name).with("detail", failed->detail);
    }
    e.with("package", directory.string());
    return e;
}

std::vector<Check> verify_report(const std::filesystem::path& directory, const VerifyOptions& options) {
    return verify_impl(directory, options, /*stop_early=*/false).checks;
}

}  // namespace twin::package
