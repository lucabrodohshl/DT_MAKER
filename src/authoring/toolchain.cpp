/**
 * @file toolchain.cpp
 * @brief Model artefact content -> toolchain bytes; compilation of canonical models.
 */
#include "twin/authoring/toolchain.hpp"

#include <atomic>
#include <fstream>
#include <unistd.h>

#include "twin/authoring/uppaal.hpp"
#include "twin/authoring/validate.hpp"
#include "twin/core/sha256.hpp"

namespace twin::authoring {

std::string_view content_format(std::string_view content) {
    std::size_t i = 0;
    if (content.substr(0, 3) == "\xEF\xBB\xBF") i = 3;  // UTF-8 byte-order mark
    while (i < content.size() && (content[i] == ' ' || content[i] == '\n' || content[i] == '\r' || content[i] == '\t')) ++i;
    if (i >= content.size()) return "unknown";
    if (content[i] == '<') return "uppaal-xml";
    if (content[i] == '{') {
        Result<json::Json> j = json::parse(content.substr(i));
        if (j && j.value().is_object() && j.value().contains("format") && j.value().at("format") == kModelFormat) {
            return kModelFormat;
        }
    }
    return "unknown";
}

Result<std::string> toolchain_source(std::string_view content) {
    const std::string_view format = content_format(content);
    if (format == "uppaal-xml") return std::string(content);
    if (format != kModelFormat) {
        return make_error(ErrorCode::InvalidArgument, "model content is neither twin-ta/1 nor UPPAAL XML");
    }
    Result<json::Json> j = json::parse(content);
    if (!j) return std::move(j).error();
    Result<Model> m = model_from_json(j.value());
    if (!m) return std::move(m).error();
    const std::vector<Diagnostic> ds = validate(m.value());
    for (const Diagnostic& d : ds) {
        if (d.severity == "error") {
            return make_error(ErrorCode::ValidationError, "the model is not valid: " + d.message)
                .with("code", d.code)
                .with("element", d.element.kind + " " + d.element.name);
        }
    }
    return render_toolchain_xml(m.value());
}

bool write_file_atomically(const std::filesystem::path& file, std::string_view bytes) {
    static std::atomic<std::uint64_t> counter{0};
    const std::filesystem::path tmp =
        file.string() + ".tmp-" + std::to_string(::getpid()) + "-" + std::to_string(counter.fetch_add(1));
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << bytes;
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);  // atomic: readers see the old or the new complete file
    if (ec) std::filesystem::remove(tmp, ec);
    return !ec;
}

json::Json source_map(const Model& model, const ir::Model& ir) {
    json::Json locations = json::Json::object();
    for (const ir::Location& l : ir.locations) locations[l.id] = l.id;
    json::Json transitions = json::Json::object();
    json::Json edges = json::Json::object();
    for (std::size_t k = 0; k < ir.transitions.size() && k < model.edges.size(); ++k) {
        transitions[ir.transitions[k].id] = model.edges[k].id;
        edges[model.edges[k].id] = ir.transitions[k].id;
    }
    return json::Json{{"locations", locations}, {"transitions", transitions}, {"edges", edges}};
}

std::variant<CompiledModel, compiler::CompileFailure> compile_model(const Model& model,
                                                                    const compiler::CompileOptions& options,
                                                                    const std::filesystem::path& work_dir) {
    compiler::CompileFailure invalid;
    for (const Diagnostic& d : validate(model)) {
        if (d.severity != "error") continue;
        invalid.diagnostics.push_back(compiler::Diagnostic{compiler::Severity::Error, d.code, d.message,
                                                           d.element.kind + " " + d.element.name, d.hint});
    }
    if (!invalid.diagnostics.empty()) return invalid;

    const std::string xml = render_toolchain_xml(model);
    const std::string digest = sha256_hex(xml);
    std::error_code ec;
    std::filesystem::create_directories(work_dir, ec);
    const std::filesystem::path file = work_dir / (digest + ".xml");
    if (!write_file_atomically(file, xml)) {
        invalid.diagnostics.push_back(compiler::Diagnostic{compiler::Severity::Error, "TWC000",
                                                           "cannot write the model rendering", file.string(), ""});
        return invalid;
    }
    auto outcome = compiler::compile_file(file, options);
    if (auto* failure = std::get_if<compiler::CompileFailure>(&outcome)) return std::move(*failure);
    auto& result = std::get<compiler::CompileResult>(outcome);
    json::Json map = source_map(model, result.model);
    return CompiledModel{std::move(result), std::move(map), digest};
}

}  // namespace twin::authoring
