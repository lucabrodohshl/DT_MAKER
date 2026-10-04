/**
 * @file interpretation.cpp
 * @brief Reader for `.interp` files (same line grammar as the aligner).
 */
#include "interpretation.hpp"

#include <fstream>
#include <sstream>

#include "twin/core/sha256.hpp"

namespace twin::compiler::detail {
namespace {

std::string trim(const std::string& s) {
    const std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/// Strip a trailing comment: ';' outside parentheses (as the aligner does).
std::string strip_comment(const std::string& s) {
    int depth = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '(') ++depth;
        else if (s[i] == ')') --depth;
        else if (s[i] == ';' && depth == 0) return trim(s.substr(0, i));
    }
    return trim(s);
}

}  // namespace

std::optional<InterpretationEntries> read_interpretation(const std::filesystem::path& path,
                                                         std::vector<Diagnostic>& diagnostics) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        diagnostics.push_back(Diagnostic{Severity::Error, "TWC070", "cannot open interpretation file",
                                         path.string(), {}});
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string bytes = buffer.str();
    InterpretationEntries entries;
    entries.sha256 = sha256_hex(bytes);

    std::istringstream lines(bytes);
    std::string raw;
    std::size_t line_no = 0;
    bool ok = true;
    while (std::getline(lines, raw)) {
        ++line_no;
        std::string line = trim(raw);
        if (line.empty() || line[0] == ';') continue;
        line = strip_comment(line);
        if (line.empty()) continue;
        const std::size_t colon = line.find(':');
        const std::string where = path.filename().string() + ":" + std::to_string(line_no);
        if (colon == std::string::npos) {
            diagnostics.push_back(Diagnostic{Severity::Warning, "TWC071",
                                             "line without ':' is ignored (as by the aligner)", where, {}});
            continue;
        }
        const std::string key = trim(line.substr(0, colon));
        const std::string formula = trim(line.substr(colon + 1));
        if (key.empty() || formula.empty()) {
            continue;
        }
        if (key.back() == '?') {
            diagnostics.push_back(Diagnostic{
                Severity::Error, "TWC072", "receive label '" + key + "' cannot be interpreted", where,
                "the aligner's .interp format only recognises send labels ('a!'); it would treat '" +
                    key + "' as a location name"});
            ok = false;
            continue;
        }
        auto& table = key.back() == '!' ? entries.events : entries.locations;
        if (!table.emplace(key, formula).second) {
            diagnostics.push_back(Diagnostic{Severity::Error, "TWC073",
                                             "duplicate interpretation of '" + key + "'", where,
                                             "the aligner would silently keep only the last one"});
            ok = false;
        }
    }
    if (!ok) {
        return std::nullopt;
    }
    return entries;
}

}  // namespace twin::compiler::detail
