/**
 * @file diagnostics.cpp
 * @brief JSON encoding of authoring diagnostics.
 */
#include "twin/authoring/diagnostics.hpp"

#include <algorithm>

namespace twin::authoring {

json::Json to_json(const SourceRange& r) {
    return json::Json{{"line", r.line}, {"column", r.column}, {"endLine", r.end_line}, {"endColumn", r.end_column}};
}

json::Json to_json(const Diagnostic& d) {
    json::Json j{{"severity", d.severity},
                 {"code", d.code},
                 {"message", d.message},
                 {"hint", d.hint},
                 {"element", {{"kind", d.element.kind}, {"name", d.element.name}, {"part", d.element.part}}}};
    if (d.range) j["range"] = to_json(*d.range);
    return j;
}

json::Json to_json(const std::vector<Diagnostic>& diagnostics) {
    json::Json out = json::Json::array();
    for (const Diagnostic& d : diagnostics) out.push_back(to_json(d));
    return out;
}

bool has_errors(const std::vector<Diagnostic>& diagnostics) noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const Diagnostic& d) { return d.severity == "error"; });
}

}  // namespace twin::authoring
