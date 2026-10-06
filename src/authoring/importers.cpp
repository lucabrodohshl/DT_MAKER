/**
 * @file importers.cpp
 * @brief Importer registry (UPPAAL XML, twin-ta/1 JSON, TwinTA text), JSON encoding and export.
 */
#include <string>

#include "twin/authoring/import.hpp"
#include "twin/authoring/text.hpp"
#include "twin/authoring/toolchain.hpp"
#include "twin/authoring/uppaal.hpp"
#include "twin/authoring/validate.hpp"
#include "twin/core/sha256.hpp"
#include "twin/core/wall_clock.hpp"

namespace twin::authoring {
namespace {

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

/// Provenance of a lossless (canonical or textual) import.
Provenance lossless(std::string_view content, const ImportOptions& options, const std::string& importer,
                    const Model& m) {
    Provenance p;
    p.original_filename = options.filename;
    p.original_sha256 = sha256_hex(content);
    p.imported_at = now_utc_millis();
    p.importer = importer;
    p.importer_version = std::string(kImporterVersion);
    p.content_sha256 = content_sha256(m);
    p.semantic_digest = semantic_digest(m);
    p.preserved = true;  // the input format is a lossless form of the canonical model
    p.preservation_checks = 0;
    return p;
}

class UppaalImporter final : public Importer {
public:
    std::string id() const override { return "uppaal-xml"; }
    std::string version() const override { return std::string(kImporterVersion); }
    std::string description() const override {
        return "UPPAAL XML timed automaton (one template), read by the compiler's strict reader";
    }
    bool detect(std::string_view /*filename*/, std::string_view content) const override {
        return content_format(content) == "uppaal-xml" && content.find("<nta") != std::string_view::npos;
    }
    ImportResult run(std::string_view content, const ImportOptions& options) const override {
        return import_uppaal_xml(content, options);
    }
};

class JsonImporter final : public Importer {
public:
    std::string id() const override { return "twin-ta-json"; }
    std::string version() const override { return std::string(kImporterVersion); }
    std::string description() const override { return "Canonical timed-automaton model (twin-ta/1 JSON)"; }
    bool detect(std::string_view /*filename*/, std::string_view content) const override {
        return content_format(content) == kModelFormat;
    }
    ImportResult run(std::string_view content, const ImportOptions& options) const override {
        ImportResult r;
        r.format = id();
        Result<json::Json> j = json::parse(content);
        Result<Model> m = j ? model_from_json(j.value()) : Result<Model>(j.error());
        if (!m) {
            r.diagnostics.push_back(Diagnostic{"error", "TWI003", m.error().to_string(), "", {"document", options.filename, ""},
                                               std::nullopt});
            return r;
        }
        r.diagnostics = validate(m.value());
        if (has_errors(r.diagnostics)) return r;
        r.provenance = lossless(content, options, id(), m.value());
        r.model = std::move(m).value();
        return r;
    }
};

class TextImporter final : public Importer {
public:
    std::string id() const override { return "twinta-text"; }
    std::string version() const override { return std::string(kImporterVersion); }
    std::string description() const override { return "TwinTA text (the textual form of the canonical model)"; }
    bool detect(std::string_view filename, std::string_view content) const override {
        if (ends_with(filename, ".tta") || ends_with(filename, ".twinta")) return true;
        // first significant token is the keyword `automaton` (comments skipped)
        std::size_t i = 0;
        while (i < content.size()) {
            if (std::isspace(static_cast<unsigned char>(content[i])) != 0) {
                ++i;
            } else if (content.substr(i, 2) == "//") {
                i = content.find('\n', i);
                if (i == std::string_view::npos) return false;
            } else if (content.substr(i, 2) == "/*") {
                i = content.find("*/", i);
                if (i == std::string_view::npos) return false;
                i += 2;
            } else {
                return content.substr(i, 9) == "automaton";
            }
        }
        return false;
    }
    ImportResult run(std::string_view content, const ImportOptions& options) const override {
        ImportResult r;
        r.format = id();
        ParseResult p = parse_text(content);
        r.diagnostics = std::move(p.diagnostics);
        if (!p.model || has_errors(r.diagnostics)) return r;
        r.provenance = lossless(content, options, id(), *p.model);
        r.model = std::move(p.model);
        return r;
    }
};

}  // namespace

const std::vector<const Importer*>& importers() {
    static const UppaalImporter uppaal;
    static const JsonImporter json;
    static const TextImporter text;
    static const std::vector<const Importer*> all = {&uppaal, &json, &text};
    return all;
}

ImportResult import_any(std::string_view content, const ImportOptions& options) {
    for (const Importer* i : importers()) {
        if (i->detect(options.filename, content)) return i->run(content, options);
    }
    ImportResult r;
    r.format = "unknown";
    r.diagnostics.push_back(Diagnostic{"error", "TWI001", "the file is not in a supported model format",
                                       "supported: UPPAAL XML, twin-ta/1 JSON, TwinTA text (.tta)",
                                       {"document", options.filename, ""}, std::nullopt});
    return r;
}

json::Json to_json(const Provenance& p) {
    return json::Json{{"originalFilename", p.original_filename},
                      {"originalSha256", p.original_sha256},
                      {"importedAt", p.imported_at},
                      {"importer", p.importer},
                      {"importerVersion", p.importer_version},
                      {"options", p.options},
                      {"contentSha256", p.content_sha256},
                      {"semanticDigest", p.semantic_digest},
                      {"preserved", p.preserved},
                      {"preservationChecks", static_cast<std::int64_t>(p.preservation_checks)}};
}

json::Json to_json(const ImportResult& r) {
    json::Json j{{"format", r.format}, {"layout", to_json(r.layout)}, {"diagnostics", to_json(r.diagnostics)}};
    if (r.model) j["model"] = to_json(*r.model);
    if (r.provenance) j["provenance"] = to_json(*r.provenance);
    return j;
}

Result<ExportResult> export_model(const Model& model, const Layout& layout, std::string_view target) {
    for (const Diagnostic& d : validate(model)) {
        if (d.severity == "error") {
            return make_error(ErrorCode::ValidationError, "the model is not valid: " + d.message).with("code", d.code);
        }
    }
    ExportResult out;
    if (target == "twinta") {
        out.content = print_text(model);
        out.media_type = "text/plain";
        const ParseResult back = parse_text(out.content);
        out.round_trip_verified = back.model && *back.model == model;
    } else if (target == "json") {
        out.content = to_json(model).dump(2) + "\n";
        out.media_type = "application/json";
        Result<json::Json> j = json::parse(out.content);
        Result<Model> back = j ? model_from_json(j.value()) : Result<Model>(j.error());
        out.round_trip_verified = back && back.value() == model;
    } else if (target == "uppaal" || target == "uppaal-toolchain") {
        out.content = target == "uppaal" ? render_exchange_xml(model, layout) : render_toolchain_xml(model);
        out.media_type = "application/xml";
        const ImportResult back = import_uppaal_xml(out.content, ImportOptions{"export.xml", false});
        out.round_trip_verified = back.model && semantic_digest(*back.model) == semantic_digest(model);
    } else {
        return make_error(ErrorCode::InvalidArgument, "unknown export target '" + std::string(target) + "'")
            .with("supported", "twinta, json, uppaal, uppaal-toolchain");
    }
    if (!out.round_trip_verified) {
        return make_error(ErrorCode::IntegrityError, "the export does not round-trip; refusing to produce it")
            .with("target", std::string(target));
    }
    return out;
}

}  // namespace twin::authoring
