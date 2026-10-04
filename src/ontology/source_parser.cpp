/**
 * @file source_parser.cpp
 * @brief Strict parser for the aligner's .ont / .interp formats (see source.hpp).
 *
 * Line handling deliberately mirrors `dtpta::OntFileParser` /
 * `dtpta::InterpFileParser` (SemPTDTAlignmentICSE/src/semalign/domain_parser.cpp):
 * trim, skip blank and `;` lines, strip the trailing comment (a `;` outside
 * parentheses), split at the first ':'. Where the aligner silently skips a
 * line, this parser reports an error instead.
 */
#include "twin/ontology/source.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <unordered_set>

namespace twin::ontology {

const char* to_string(Severity severity) noexcept {
    switch (severity) {
        case Severity::Error: return "error";
        case Severity::Warning: return "warning";
        case Severity::Note: return "note";
    }
    return "error";
}

namespace {

template <class T>
const T* find_named(const std::vector<T>& items, std::string_view name) noexcept {
    for (const auto& item : items) {
        if (item.name == name) return &item;
    }
    return nullptr;
}

bool has_error(const std::vector<SourceDiagnostic>& diagnostics) noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const SourceDiagnostic& d) { return d.severity == Severity::Error; });
}

bool is_space(char c) noexcept { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/// @brief A physical line with the offset of its first byte.
struct Line {
    std::string_view text;
    std::uint32_t number{0};
};

std::vector<Line> split_lines(std::string_view text) {
    std::vector<Line> lines;
    std::uint32_t number = 1;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back({line, number++});
        if (end == text.size()) break;
        start = end + 1;
    }
    return lines;
}

/// @brief [begin, end) of the trimmed region of @p s.
std::pair<std::size_t, std::size_t> trimmed_bounds(std::string_view s, std::size_t begin, std::size_t end) {
    while (begin < end && is_space(s[begin])) ++begin;
    while (end > begin && is_space(s[end - 1])) --end;
    return {begin, end};
}

/// @brief Position of the comment `;` outside parentheses, or npos (aligner's strip_comment).
std::size_t comment_start(std::string_view s) noexcept {
    int depth = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '(') ++depth;
        else if (s[i] == ')') --depth;
        else if (s[i] == ';' && depth == 0) return i;
    }
    return std::string_view::npos;
}

std::string comment_text(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ';' || is_space(s[i]))) ++i;
    auto [b, e] = trimmed_bounds(s, i, s.size());
    return std::string(s.substr(b, e - b));
}

bool is_identifier(std::string_view s) noexcept {
    if (s.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    });
}

Span span_of(const Line& line, std::size_t begin, std::size_t end) {
    return Span{line.number, static_cast<std::uint32_t>(begin + 1), static_cast<std::uint32_t>(end - begin)};
}

/// @brief Whitespace tokens of line[begin,end) with their spans.
struct Token {
    std::string text;
    std::size_t begin{0};
    std::size_t end{0};
};

std::vector<Token> tokens(std::string_view s, std::size_t begin, std::size_t end) {
    std::vector<Token> out;
    std::size_t i = begin;
    while (i < end) {
        while (i < end && is_space(s[i])) ++i;
        std::size_t j = i;
        while (j < end && !is_space(s[j])) ++j;
        if (j > i) out.push_back({std::string(s.substr(i, j - i)), i, j});
        i = j;
    }
    return out;
}

// ---------------------------------------------------------------------------
// SMT-LIB2 s-expressions (for symbol extraction and well-formedness only; the
// authoritative interpretation of formulas is Z3 via the aligner, theory.hpp).
// ---------------------------------------------------------------------------

/// @brief Aligner's C-style call rewriting "f(a,b)" -> "(f a b)" (domain_parser.cpp: cstyle_to_smt2).
std::string cstyle_to_smt2(std::string_view s) {
    std::string result;
    result.reserve(s.size() + 16);
    std::size_t i = 0;
    while (i < s.size()) {
        if (std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_') {
            std::size_t j = i;
            while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
            if (j < s.size() && s[j] == '(') {
                result += '(';
                result += s.substr(i, j - i);
                result += ' ';
                ++j;
                int depth = 1;
                std::string arg;
                auto flush = [&](bool trailing_space) {
                    auto [b, e] = trimmed_bounds(arg, 0, arg.size());
                    if (e > b) {
                        result += cstyle_to_smt2(std::string_view(arg).substr(b, e - b));
                        if (trailing_space) result += ' ';
                    }
                    arg.clear();
                };
                while (j < s.size() && depth > 0) {
                    const char c = s[j];
                    if (c == '(') {
                        ++depth;
                        arg += c;
                    } else if (c == ')') {
                        --depth;
                        if (depth == 0) flush(false);
                        else arg += c;
                    } else if (c == ',' && depth == 1) {
                        flush(true);
                    } else {
                        arg += c;
                    }
                    ++j;
                }
                result += ')';
                i = j;
                continue;
            }
            result += s.substr(i, j - i);
            i = j;
            continue;
        }
        result += s[i];
        ++i;
    }
    return result;
}

struct SExpr {
    bool is_atom{true};
    std::string atom;
    std::vector<SExpr> items;
};

class SExprReader {
public:
    explicit SExprReader(std::string_view text) : text_(text) {}

    /// @brief Read every top-level expression; false on malformed input.
    bool read_all(std::vector<SExpr>& out) {
        skip_space();
        while (pos_ < text_.size()) {
            SExpr e;
            if (!read(e, 0)) return false;
            out.push_back(std::move(e));
            skip_space();
        }
        return true;
    }

private:
    static constexpr int kMaxDepth = 512;

    void skip_space() {
        while (pos_ < text_.size() && is_space(text_[pos_])) ++pos_;
    }

    bool read(SExpr& out, int depth) {
        if (depth > kMaxDepth) return false;
        skip_space();
        if (pos_ >= text_.size()) return false;
        const char c = text_[pos_];
        if (c == ')') return false;
        if (c == '(') {
            ++pos_;
            out.is_atom = false;
            for (;;) {
                skip_space();
                if (pos_ >= text_.size()) return false;
                if (text_[pos_] == ')') {
                    ++pos_;
                    return true;
                }
                SExpr item;
                if (!read(item, depth + 1)) return false;
                out.items.push_back(std::move(item));
            }
        }
        if (c == '|' || c == '"') {  // quoted symbol / string literal
            const char close = c;
            std::size_t end = text_.find(close, pos_ + 1);
            if (end == std::string_view::npos) return false;
            out.atom = std::string(text_.substr(pos_, end - pos_ + 1));
            pos_ = end + 1;
            return true;
        }
        std::size_t start = pos_;
        while (pos_ < text_.size() && !is_space(text_[pos_]) && text_[pos_] != '(' && text_[pos_] != ')') ++pos_;
        out.atom = std::string(text_.substr(start, pos_ - start));
        return true;
    }

    std::string_view text_;
    std::size_t pos_{0};
};

const std::unordered_set<std::string>& smt_builtins() {
    static const std::unordered_set<std::string> kBuiltins = {
        "true", "false", "True", "False", "TRUE", "FALSE", "and", "or", "not", "=>", "implies", "xor",
        "ite", "=", "distinct", "+", "-", "*", "/", "div", "mod", "abs", "<", "<=", ">", ">=",
        "to_real", "to_int", "is_int", "^", "Int", "Real", "Bool", "as", "!", "forall", "exists", "let"};
    return kBuiltins;
}

bool is_numeral(std::string_view atom) noexcept {
    if (atom.empty()) return false;
    if (atom.size() > 2 && atom[0] == '#' && (atom[1] == 'x' || atom[1] == 'b')) return true;
    // Z3's SMT-LIB front end (and therefore the aligner) also accepts "-12" as a literal.
    if (atom.size() > 1 && atom[0] == '-') atom.remove_prefix(1);
    bool digit = false;
    for (char c : atom) {
        if (std::isdigit(static_cast<unsigned char>(c))) digit = true;
        else if (c != '.') return false;
    }
    return digit;
}

void collect(const SExpr& e, std::vector<std::string>& bound, std::set<std::string>& out) {
    if (e.is_atom) {
        const std::string& a = e.atom;
        if (a.empty() || a[0] == ':' || a[0] == '"' || is_numeral(a) || smt_builtins().count(a) != 0) return;
        if (std::find(bound.begin(), bound.end(), a) != bound.end()) return;
        out.insert(a);
        return;
    }
    if (e.items.empty()) return;
    const SExpr& head = e.items.front();
    if (head.is_atom && (head.atom == "forall" || head.atom == "exists") && e.items.size() >= 3 &&
        !e.items[1].is_atom) {
        const std::size_t mark = bound.size();
        for (const SExpr& binder : e.items[1].items) {
            if (!binder.is_atom && !binder.items.empty() && binder.items.front().is_atom) {
                bound.push_back(binder.items.front().atom);  // sort names (binder.items[1]) are not symbols
            }
        }
        for (std::size_t i = 2; i < e.items.size(); ++i) collect(e.items[i], bound, out);
        bound.resize(mark);
        return;
    }
    if (head.is_atom && head.atom == "let" && e.items.size() >= 3 && !e.items[1].is_atom) {
        const std::size_t mark = bound.size();
        std::vector<std::string> names;
        for (const SExpr& binding : e.items[1].items) {
            if (!binding.is_atom && binding.items.size() == 2 && binding.items[0].is_atom) {
                collect(binding.items[1], bound, out);  // let is parallel: values in the outer scope
                names.push_back(binding.items[0].atom);
            }
        }
        bound.insert(bound.end(), names.begin(), names.end());
        for (std::size_t i = 2; i < e.items.size(); ++i) collect(e.items[i], bound, out);
        bound.resize(mark);
        return;
    }
    for (const SExpr& item : e.items) collect(item, bound, out);
}

// ---------------------------------------------------------------------------

/// @brief Common per-line pre-processing: code region, comment and header handling.
struct LineParts {
    std::size_t code_begin{0};
    std::size_t code_end{0};
    std::string comment;
    bool blank{true};
    bool comment_only{false};
};

LineParts split_line(std::string_view line) {
    LineParts parts;
    auto [b, e] = trimmed_bounds(line, 0, line.size());
    if (b == e) return parts;
    if (line[b] == ';') {
        parts.comment_only = true;
        parts.comment = comment_text(line.substr(b));
        return parts;
    }
    parts.blank = false;
    std::string_view region = line.substr(b, e - b);
    const std::size_t semi = comment_start(region);
    std::size_t code_end = e;
    if (semi != std::string_view::npos) {
        parts.comment = comment_text(region.substr(semi));
        code_end = b + semi;
    }
    auto [cb, ce] = trimmed_bounds(line, b, code_end);
    parts.code_begin = cb;
    parts.code_end = ce;
    parts.blank = (cb == ce);
    return parts;
}

/// @brief Accumulates the leading comment block of a document.
class HeaderCollector {
public:
    void comment(const std::string& text) {
        if (done_) return;
        if (text.empty() || is_banner(text)) return;
        if (!header_.empty()) header_ += '\n';
        header_ += text;
    }
    void declaration() noexcept { done_ = true; }
    [[nodiscard]] std::string take() { return std::move(header_); }

private:
    static bool is_banner(const std::string& text) {
        return text.find("===") != std::string::npos;
    }
    std::string header_;
    bool done_{false};
};

bool check_formula(const std::string& formula, const Span& span, const char* code,
                   std::vector<SourceDiagnostic>& diags) {
    if (formula.empty()) {
        diags.push_back({code, Severity::Error, "missing formula after ':'", span});
        return false;
    }
    if (!formula_symbols(formula)) {
        diags.push_back({code, Severity::Error,
                         "formula is not a well-formed SMT-LIB2 expression (check parentheses): " + formula, span});
        return false;
    }
    return true;
}

}  // namespace

bool is_builtin_sort(std::string_view name) noexcept {
    return name == "Int" || name == "Real" || name == "Bool";
}

const SortDecl* OntologySource::find_sort(std::string_view name) const noexcept { return find_named(sorts, name); }
const FunctionDecl* OntologySource::find_function(std::string_view name) const noexcept {
    return find_named(functions, name);
}
const RelationDecl* OntologySource::find_relation(std::string_view name) const noexcept {
    return find_named(relations, name);
}
const Axiom* OntologySource::find_axiom(std::string_view id) const noexcept {
    for (const auto& a : axioms) {
        if (a.id == id) return &a;
    }
    return nullptr;
}
const InterpretationEntry* InterpretationSource::find(std::string_view key) const noexcept {
    for (const auto& e : entries) {
        if (e.key == key) return &e;
    }
    return nullptr;
}

bool ParsedOntology::ok() const noexcept { return !has_error(diagnostics); }
bool ParsedInterpretation::ok() const noexcept { return !has_error(diagnostics); }

std::optional<std::vector<std::string>> formula_symbols(std::string_view smt2) {
    const std::string converted = cstyle_to_smt2(smt2);
    std::vector<SExpr> exprs;
    SExprReader reader(converted);
    if (!reader.read_all(exprs) || exprs.empty()) return std::nullopt;
    std::set<std::string> symbols;
    std::vector<std::string> bound;
    for (const SExpr& e : exprs) collect(e, bound, symbols);
    return std::vector<std::string>(symbols.begin(), symbols.end());
}

ParsedOntology parse_ontology(std::string_view text) {
    ParsedOntology result;
    auto& src = result.source;
    auto& diags = result.diagnostics;
    HeaderCollector header;
    std::set<std::string> declared;  // all symbol names (sorts, functions, relations)

    for (const Line& line : split_lines(text)) {
        const LineParts parts = split_line(line.text);
        if (parts.comment_only) {
            header.comment(parts.comment);
            continue;
        }
        if (parts.blank) continue;
        header.declaration();

        const std::string_view s = line.text;
        std::size_t kw_end = parts.code_begin;
        while (kw_end < parts.code_end && !is_space(s[kw_end])) ++kw_end;
        const std::string keyword(s.substr(parts.code_begin, kw_end - parts.code_begin));
        const Span kw_span = span_of(line, parts.code_begin, kw_end);

        if (keyword == "sort") {
            const auto toks = tokens(s, kw_end, parts.code_end);
            if (toks.empty()) {
                diags.push_back({"ONT003", Severity::Error, "'sort' declaration without a name", kw_span});
                continue;
            }
            const Span name_span = span_of(line, toks[0].begin, toks[0].end);
            if (toks.size() > 1) {
                diags.push_back({"ONT009", Severity::Error,
                                 "unexpected text after sort name '" + toks[0].text + "'",
                                 span_of(line, toks[1].begin, toks.back().end)});
            }
            if (!is_identifier(toks[0].text)) {
                diags.push_back({"ONT009", Severity::Error, "invalid sort name '" + toks[0].text + "'", name_span});
                continue;
            }
            if (is_builtin_sort(toks[0].text)) {
                diags.push_back({"ONT010", Severity::Warning,
                                 "'" + toks[0].text + "' is a built-in sort; the declaration has no effect", name_span});
            }
            if (!declared.insert(toks[0].text).second) {
                diags.push_back({"ONT004", Severity::Error, "duplicate declaration of '" + toks[0].text + "'", name_span});
                continue;
            }
            src.sorts.push_back({toks[0].text, parts.comment, name_span});
            continue;
        }

        if (keyword == "fun" || keyword == "rel" || keyword == "axiom") {
            const std::size_t colon = s.substr(0, parts.code_end).find(':', kw_end);
            if (colon == std::string_view::npos) {
                diags.push_back({"ONT002", Severity::Error, "'" + keyword + "' declaration is missing ':'",
                                 span_of(line, parts.code_begin, parts.code_end)});
                continue;
            }
            auto [nb, ne] = trimmed_bounds(s, kw_end, colon);
            const std::string name(s.substr(nb, ne - nb));
            const Span name_span = ne > nb ? span_of(line, nb, ne) : kw_span;
            auto [rb, re] = trimmed_bounds(s, colon + 1, parts.code_end);
            const std::string rest(s.substr(rb, re - rb));
            const Span rest_span = span_of(line, rb, re);

            if (name.empty()) {
                diags.push_back({"ONT003", Severity::Error, "'" + keyword + "' declaration without a name", kw_span});
                continue;
            }
            if (!is_identifier(name)) {
                diags.push_back({"ONT009", Severity::Error, "invalid identifier '" + name + "'", name_span});
                continue;
            }

            if (keyword == "axiom") {
                if (src.find_axiom(name) != nullptr) {
                    diags.push_back({"ONT008", Severity::Error, "duplicate axiom id '" + name + "'", name_span});
                    continue;
                }
                check_formula(rest, rest_span, "ONT006", diags);
                src.axioms.push_back({name, rest, parts.comment, name_span, rest_span});
                continue;
            }

            if (!declared.insert(name).second) {
                diags.push_back({"ONT004", Severity::Error, "duplicate declaration of '" + name + "'", name_span});
                continue;
            }
            if (keyword == "fun") {
                FunctionDecl fd{name, {}, {}, parts.comment, name_span};
                const std::size_t arrow = rest.find("->");
                if (arrow == std::string::npos) {
                    const auto toks = tokens(rest, 0, rest.size());
                    if (toks.size() != 1) {
                        diags.push_back({"ONT002", Severity::Error,
                                         "function '" + name + "' needs a signature 'Ret' or 'Arg ... -> Ret'", rest_span});
                        continue;
                    }
                    fd.return_sort = toks[0].text;
                } else {
                    for (const Token& t : tokens(rest, 0, arrow)) fd.arg_sorts.push_back(t.text);
                    const auto ret = tokens(rest, arrow + 2, rest.size());
                    if (ret.size() != 1) {
                        diags.push_back({"ONT002", Severity::Error,
                                         "function '" + name + "' needs exactly one result sort after '->'", rest_span});
                        continue;
                    }
                    fd.return_sort = ret[0].text;
                }
                src.functions.push_back(std::move(fd));
            } else {
                RelationDecl rd{name, {}, parts.comment, name_span};
                for (const Token& t : tokens(rest, 0, rest.size())) rd.arg_sorts.push_back(t.text);
                src.relations.push_back(std::move(rd));
            }
            continue;
        }

        diags.push_back({"ONT001", Severity::Error,
                         "unknown keyword '" + keyword +
                             "' (expected sort, fun, rel or axiom); the aligner would silently ignore this line",
                         kw_span});
    }

    // Sort references (the aligner maps undeclared sorts to Real: legal, but suspicious).
    auto check_sort = [&](const std::string& sort, const Span& where, const std::string& owner) {
        if (is_builtin_sort(sort) || src.find_sort(sort) != nullptr) return;
        diags.push_back({"ONT005", Severity::Warning,
                         "sort '" + sort + "' used by '" + owner + "' is not declared; the aligner reads it as Real",
                         where});
    };
    for (const auto& f : src.functions) {
        for (const auto& a : f.arg_sorts) check_sort(a, f.span, f.name);
        check_sort(f.return_sort, f.span, f.name);
    }
    for (const auto& r : src.relations) {
        for (const auto& a : r.arg_sorts) check_sort(a, r.span, r.name);
    }

    // Symbols used by axioms must be declared.
    for (const auto& ax : src.axioms) {
        const auto symbols = formula_symbols(ax.formula);
        if (!symbols) continue;  // already reported as ONT006
        for (const auto& sym : *symbols) {
            if (src.find_function(sym) == nullptr && src.find_relation(sym) == nullptr &&
                src.find_sort(sym) == nullptr) {
                diags.push_back({"ONT007", Severity::Error,
                                 "axiom '" + ax.id + "' uses undeclared symbol '" + sym + "'", ax.formula_span});
            }
        }
    }

    src.header_comment = header.take();
    std::stable_sort(diags.begin(), diags.end(), [](const SourceDiagnostic& a, const SourceDiagnostic& b) {
        return a.span.line < b.span.line;
    });
    return result;
}

ParsedInterpretation parse_interpretation(std::string_view text, const OntologySource* ontology) {
    ParsedInterpretation result;
    auto& src = result.source;
    auto& diags = result.diagnostics;
    HeaderCollector header;

    for (const Line& line : split_lines(text)) {
        const LineParts parts = split_line(line.text);
        if (parts.comment_only) {
            header.comment(parts.comment);
            continue;
        }
        if (parts.blank) continue;
        header.declaration();

        const std::string_view s = line.text;
        const std::size_t colon = s.substr(0, parts.code_end).find(':', parts.code_begin);
        if (colon == std::string_view::npos) {
            diags.push_back({"INT001", Severity::Error,
                             "expected 'Location : formula' or 'label! : formula'; the aligner would silently skip this line",
                             span_of(line, parts.code_begin, parts.code_end)});
            continue;
        }
        auto [kb, ke] = trimmed_bounds(s, parts.code_begin, colon);
        auto [fb, fe] = trimmed_bounds(s, colon + 1, parts.code_end);
        const std::string key(s.substr(kb, ke - kb));
        const std::string formula(s.substr(fb, fe - fb));
        const Span key_span = span_of(line, kb, ke);
        const Span formula_span = span_of(line, fb, fe);

        if (key.empty() || formula.empty()) {
            diags.push_back({"INT001", Severity::Error,
                             key.empty() ? "missing location/label before ':'" : "missing formula after ':'",
                             span_of(line, parts.code_begin, parts.code_end)});
            continue;
        }
        const bool is_event = key.back() == '!';
        const std::string bare = is_event ? key.substr(0, key.size() - 1) : key;
        if (!is_identifier(bare)) {
            diags.push_back({"INT005", Severity::Error, "invalid location or label name '" + key + "'", key_span});
            continue;
        }
        if (src.find(key) != nullptr) {
            diags.push_back({"INT002", Severity::Error,
                             "duplicate interpretation of '" + key + "' (the aligner keeps only the last one)", key_span});
            continue;
        }
        if (check_formula(formula, formula_span, "INT003", diags) && ontology != nullptr) {
            const std::vector<std::string> symbols = formula_symbols(formula).value_or(std::vector<std::string>{});
            for (const auto& sym : symbols) {
                if (ontology->find_function(sym) == nullptr && ontology->find_relation(sym) == nullptr &&
                    ontology->find_sort(sym) == nullptr) {
                    diags.push_back({"INT004", Severity::Error,
                                     "'" + key + "' uses symbol '" + sym + "' which the ontology does not declare",
                                     formula_span});
                }
            }
        }
        src.entries.push_back({key, is_event, formula, parts.comment, key_span, formula_span});
    }
    src.header_comment = header.take();
    return result;
}

}  // namespace twin::ontology
