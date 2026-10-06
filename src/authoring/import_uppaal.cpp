/**
 * @file import_uppaal.cpp
 * @brief UPPAAL XML -> canonical model: strict compiler reading, positions, notes, layout, preservation check.
 */
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unistd.h>

#include "twin/authoring/import.hpp"
#include "twin/authoring/uppaal.hpp"
#include "twin/authoring/validate.hpp"
#include "twin/compiler/compiler.hpp"
#include "twin/compiler/reader.hpp"
#include "twin/core/sha256.hpp"
#include "twin/core/wall_clock.hpp"
#include "xml_scan.hpp"

namespace twin::authoring {
namespace {

namespace fs = std::filesystem;
using detail::XmlNode;

std::string trim(std::string_view s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
}

/// Multi-line free text: lines trimmed, empty first/last lines dropped.
std::string normalise_note(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        lines.push_back(trim(text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start)));
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    while (!lines.empty() && lines.front().empty()) lines.erase(lines.begin());
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) out += (i > 0 ? "\n" : "") + lines[i];
    return out;
}

/// The presentational structure of a UPPAAL document.
struct XmlIndex {
    const XmlNode* document{nullptr};
    const XmlNode* nta{nullptr};
    const XmlNode* tmpl{nullptr};
    const XmlNode* global_decl{nullptr};
    const XmlNode* tmpl_decl{nullptr};
    const XmlNode* system{nullptr};
    std::map<std::string, const XmlNode*> locations;  // by name
    std::vector<const XmlNode*> transitions;
    std::string document_note;
};

XmlIndex index_document(const XmlNode& doc) {
    XmlIndex x;
    x.document = &doc;
    std::string comments;
    for (const XmlNode& c : doc.children) {
        if (c.name == "#comment" && x.nta == nullptr) comments += (comments.empty() ? "" : "\n") + c.text;
        if (c.name == "nta") x.nta = &c;
    }
    x.document_note = normalise_note(comments);
    if (x.nta == nullptr) return x;
    x.global_decl = x.nta->child("declaration");
    x.system = x.nta->child("system");
    x.tmpl = x.nta->child("template");
    if (x.tmpl == nullptr) return x;
    x.tmpl_decl = x.tmpl->child("declaration");
    for (const XmlNode* l : x.tmpl->children_named("location")) {
        if (const XmlNode* n = l->child("name")) x.locations[trim(n->text)] = l;
    }
    x.transitions = x.tmpl->children_named("transition");
    return x;
}

SourceRange at_node(const XmlNode* n) { return SourceRange{n->line, n->column, n->line, n->column}; }

/// Line of the first declaration line in @p decl that names @p ident as a whole word.
std::optional<SourceRange> identifier_line(const XmlNode* decl, std::string_view ident) {
    if (decl == nullptr) return std::nullopt;
    const std::string& text = decl->text;
    std::size_t start = 0;
    std::uint32_t k = 0;
    const auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        const std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        const std::string code = line.substr(0, line.find("//"));
        for (std::size_t p = code.find(ident); p != std::string::npos; p = code.find(ident, p + 1)) {
            const bool left = p == 0 || !word(code[p - 1]);
            const bool right = p + ident.size() >= code.size() || !word(code[p + ident.size()]);
            if (left && right) {
                const auto col = static_cast<std::uint32_t>(p + 1);
                return SourceRange{decl->text_line + k, col, decl->text_line + k, col + static_cast<std::uint32_t>(ident.size())};
            }
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
        ++k;
    }
    return at_node(decl);
}

std::string quoted(std::string_view where, std::string_view prefix) {
    const std::size_t b = where.find(prefix);
    if (b == std::string_view::npos) return {};
    const std::size_t s = b + prefix.size();
    const std::size_t e = where.find('\'', s);
    return std::string(where.substr(s, e == std::string_view::npos ? std::string_view::npos : e - s));
}

/// The label of kind @p kind of @p transition, if present.
const XmlNode* label_of(const XmlNode* transition, std::string_view kind) {
    for (const XmlNode* l : transition->children_named("label")) {
        const auto it = l->attributes.find("kind");
        if (it != l->attributes.end() && it->second == kind) return l;
    }
    return nullptr;
}

/// Element and position of a diagnostic about edge #k ("edge #k (A -> B)[, guard|, update]").
void locate_edge(Diagnostic& out, const std::string& w, const XmlIndex& x) {
    const std::size_t k = std::stoul(w.substr(6));
    std::string part;
    std::string label;
    if (w.find(", guard") != std::string::npos) {
        part = "guard";
        label = "guard";
    } else if (w.find(", update") != std::string::npos) {
        part = "reset";
        label = "assignment";
    } else if (out.code == "TWC043" || out.code == "TWC044" || out.code == "TWC045") {
        part = "sync";
        label = "synchronisation";
    }
    out.element = {"edge", "e" + std::to_string(k + 1), part};
    if (k < x.transitions.size()) {
        const XmlNode* l = label.empty() ? nullptr : label_of(x.transitions[k], label);
        out.range = at_node(l != nullptr ? l : x.transitions[k]);
    }
}

/// Position of a UTAP error "<xpath> line N" (N counts lines inside the element's text).
void locate_utap(Diagnostic& out, const std::string& w, std::size_t p, const XmlIndex& x) {
    const std::string path = w.substr(0, p);
    std::uint32_t n = 0;
    try {
        n = static_cast<std::uint32_t>(std::stoul(w.substr(p + 6)));
    } catch (const std::exception&) {
        n = 0;
    }
    const bool resolvable = !path.empty() && path != "document" && x.document != nullptr;
    const XmlNode* node = resolvable ? detail::resolve_path(*x.document, path) : nullptr;
    std::uint32_t base = 1;
    if (node != nullptr) base = node->text_line != 0 ? node->text_line : node->line;
    if (n > 0) out.range = SourceRange{base + n - 1, 1, base + n - 1, 1};
    out.element = {"document", path, ""};
}

/// A compiler diagnostic as an authoring diagnostic, with element and position.
Diagnostic convert(const compiler::Diagnostic& d, const XmlIndex& x) {
    Diagnostic out{compiler::to_string(d.severity), d.code, d.message, d.hint, {"document", "", ""}, std::nullopt};
    const std::string& w = d.where;
    if (w.starts_with("location '")) {
        const std::string name = quoted(w, "location '");
        out.element = {"location", name, w.find(", invariant") != std::string::npos ? "invariant" : ""};
        if (const auto it = x.locations.find(name); it != x.locations.end()) out.range = at_node(it->second);
    } else if (w.starts_with("edge #")) {
        locate_edge(out, w, x);
    } else if (w.find("declaration of '") != std::string::npos) {
        const std::string name = quoted(w, "declaration of '");
        out.element = {"document", name, "declaration"};
        out.range = identifier_line(w.starts_with("global") ? x.global_decl : x.tmpl_decl, name);
    } else if (w == "system declaration") {
        out.element = {"document", "system", ""};
        if (x.system != nullptr) out.range = at_node(x.system);
    } else if (w.starts_with("template ")) {
        out.element = {"model", w.substr(9), ""};
        if (x.tmpl != nullptr) out.range = at_node(x.tmpl);
    } else if (const std::size_t p = w.rfind(" line "); p != std::string::npos) {
        locate_utap(out, w, p, x);
    }
    return out;
}

/// The first name declared by a declaration line ("const int A = 1", "clock x, y", "urgent chan c").
std::string declared_name(std::string rest) {
    for (const char* kw : {"const ", "int ", "clock ", "chan ", "urgent ", "broadcast "}) {
        while (rest.starts_with(kw)) rest = trim(rest.substr(std::string_view(kw).size()));
    }
    std::size_t e = 0;
    while (e < rest.size() && (std::isalnum(static_cast<unsigned char>(rest[e])) != 0 || rest[e] == '_')) ++e;
    return rest.substr(0, e);
}

/// Notes of declarations from `//` comments in a declaration block (the lines before, or the same line).
std::map<std::string, std::string> declaration_notes(const XmlNode* decl) {
    std::map<std::string, std::string> notes;
    if (decl == nullptr) return notes;
    std::vector<std::string> pending;
    const std::string& text = decl->text;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        const std::string line = trim(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        start = nl == std::string::npos ? text.size() + 1 : nl + 1;
        const std::size_t c = line.find("//");
        const std::string code = trim(line.substr(0, c));
        const std::string comment = c == std::string::npos ? std::string() : trim(line.substr(c + 2));
        if (code.empty()) {
            if (comment.empty()) pending.clear();
            else pending.push_back(comment);
            continue;
        }
        if (!comment.empty()) pending.push_back(comment);
        const std::string name = declared_name(code);
        if (!name.empty() && !pending.empty()) {
            std::string note;
            for (std::size_t i = 0; i < pending.size(); ++i) note += (i > 0 ? "\n" : "") + pending[i];
            notes[name] = note;
        }
        pending.clear();
    }
    return notes;
}

std::string comments_label(const XmlNode* n) {
    for (const XmlNode* l : n->children_named("label")) {
        const auto it = l->attributes.find("kind");
        if (it != l->attributes.end() && it->second == "comments") return normalise_note(l->text);
    }
    return {};
}

std::optional<Point> point_of(const XmlNode* n) {
    if (n == nullptr) return std::nullopt;
    const std::optional<std::int64_t> x = n->int_attribute("x");
    const std::optional<std::int64_t> y = n->int_attribute("y");
    if (!x || !y) return std::nullopt;
    return Point{*x, *y};
}

Constraint atoms(const std::vector<compiler::SourceAtom>& in, const compiler::SourceModel& sm,
                 const std::set<std::string>& constants) {
    Constraint out;
    for (const compiler::SourceAtom& a : in) {
        Atom atom;
        atom.clock = sm.clocks.at(a.constraint.lhs - 1);
        if (a.constraint.rhs != ir::kReferenceClock) atom.minus = sm.clocks.at(a.constraint.rhs - 1);
        atom.op = a.constraint.op;
        if (a.bound_constant && constants.contains(*a.bound_constant)) {
            atom.bound = Bound{*a.bound_constant};
        } else {
            atom.bound = Bound{a.constraint.bound};
        }
        out.push_back(std::move(atom));
    }
    return out;
}

/// Edge layout (nails and the first label anchor) of a transition element.
std::optional<EdgeLayout> edge_layout(const XmlNode* t) {
    EdgeLayout el;
    for (const XmlNode* n : t->children_named("nail")) {
        if (std::optional<Point> p = point_of(n)) el.nails.push_back(*p);
    }
    for (const XmlNode* l : t->children_named("label")) {
        if (std::optional<Point> p = point_of(l)) {
            el.label = p;
            break;
        }
    }
    if (el.nails.empty() && !el.label) return std::nullopt;
    return el;
}

EdgeDecl convert_edge(std::size_t k, const compiler::SourceModel& sm, const std::set<std::string>& constants) {
    const compiler::SourceEdge& e = sm.edges[k];
    EdgeDecl edge;
    edge.id = "e" + std::to_string(k + 1);
    edge.source = sm.locations.at(e.source).name;
    edge.target = sm.locations.at(e.target).name;
    if (e.action.kind != ir::ActionKind::Internal) {
        edge.sync = Sync{e.action.channel, e.action.kind == ir::ActionKind::Send ? '!' : '?'};
    }
    edge.guard = atoms(e.guard, sm, constants);
    for (ir::ClockIndex r : e.resets) edge.resets.push_back(sm.clocks.at(r - 1));
    return edge;
}

/// The canonical model of the strict reading, with notes and layout from the document.
std::pair<Model, Layout> convert_model(const compiler::SourceModel& sm, const XmlIndex& x) {
    Model m;
    Layout layout;
    m.name = sm.template_name;
    m.note = x.document_note;
    std::map<std::string, std::string> notes = declaration_notes(x.global_decl);
    for (const auto& [k, v] : declaration_notes(x.tmpl_decl)) notes[k] = v;
    const auto note_of = [&](const std::string& name) {
        const auto it = notes.find(name);
        return it == notes.end() ? std::string() : it->second;
    };
    std::set<std::string> constant_names;
    for (const std::string& c : sm.clocks) m.clocks.push_back(ClockDecl{c, note_of(c)});
    for (const auto& [name, value] : sm.constants) {
        m.constants.push_back(ConstantDecl{name, value, note_of(name)});
        constant_names.insert(name);
    }
    for (const std::string& c : sm.channels) m.channels.push_back(ChannelDecl{c, note_of(c)});
    for (std::size_t i = 0; i < sm.locations.size(); ++i) {
        const compiler::SourceLocation& l = sm.locations[i];
        LocationDecl loc{l.name, i == sm.initial, atoms(l.invariant, sm, constant_names), ""};
        if (const auto it = x.locations.find(l.name); it != x.locations.end()) {
            loc.note = comments_label(it->second);
            if (std::optional<Point> p = point_of(it->second)) {
                layout.locations[l.name] = LocationLayout{*p, point_of(it->second->child("name"))};
            }
        }
        m.locations.push_back(std::move(loc));
    }
    for (std::size_t k = 0; k < sm.edges.size(); ++k) {
        EdgeDecl edge = convert_edge(k, sm, constant_names);
        if (k < x.transitions.size()) {
            edge.note = comments_label(x.transitions[k]);
            if (std::optional<EdgeLayout> el = edge_layout(x.transitions[k])) layout.edges[edge.id] = std::move(*el);
        }
        m.edges.push_back(std::move(edge));
    }
    return {std::move(m), std::move(layout)};
}

/// A fresh scratch directory for one preservation check.
fs::path scratch_dir() {
    static std::atomic<std::uint64_t> counter{0};
    const fs::path d = fs::temp_directory_path() /
                       ("twin-import-" + std::to_string(::getpid()) + "-" + std::to_string(counter.fetch_add(1)));
    fs::create_directories(d);
    return d;
}

std::variant<compiler::CompileResult, compiler::CompileFailure> compile_bytes(const fs::path& file, const std::string& bytes,
                                                                              bool legacy) {
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << bytes;
    }
    compiler::CompileOptions o;
    o.model_id = "import-check";
    o.legacy_system_declaration = legacy;
    return compiler::compile_file(file, o);
}

ir::Model without_source_hash(ir::Model m) {
    m.info.source_sha256.clear();
    return m;
}

}  // namespace

ImportResult import_uppaal_xml(std::string_view xml, const ImportOptions& options) {
    ImportResult r;
    r.format = "uppaal-xml";
    Result<XmlNode> doc = detail::parse_xml(xml);
    if (!doc) {
        const Error& e = doc.error();
        const auto line = static_cast<std::uint32_t>(std::stoul(std::string(e.context_value("line").empty() ? "0" : e.context_value("line"))));
        const auto col = static_cast<std::uint32_t>(std::stoul(std::string(e.context_value("column").empty() ? "0" : e.context_value("column"))));
        r.diagnostics.push_back(Diagnostic{"error", "TWI004", e.message, "the file must be a UPPAAL XML document",
                                           {"document", options.filename, ""}, SourceRange{line, col, line, col}});
        return r;
    }
    const XmlIndex x = index_document(doc.value());

    // 1. The compiler's strict reading: every unsupported construct, with its position.
    compiler::ReadResult read = compiler::read_uppaal(xml, compiler::ReaderOptions{options.legacy_system_declaration});
    for (const compiler::Diagnostic& d : read.diagnostics) r.diagnostics.push_back(convert(d, x));
    if (!read.model) return r;

    // 2. Canonical model, notes and layout.
    auto [model, layout] = convert_model(*read.model, x);
    std::vector<Diagnostic> structural = validate(model);
    if (has_errors(structural)) {
        r.diagnostics.push_back(Diagnostic{"error", "TWI003",
                                           "the imported automaton cannot be represented as a canonical model", "",
                                           {"model", model.name, ""}, std::nullopt});
        for (Diagnostic& d : structural) r.diagnostics.push_back(std::move(d));
        return r;
    }
    for (Diagnostic& d : structural) r.diagnostics.push_back(std::move(d));  // warnings only

    // 3. Preservation check: the original and the rendering compile to the same IR.
    const fs::path dir = scratch_dir();
    auto original = compile_bytes(dir / "original.xml", std::string(xml), options.legacy_system_declaration);
    auto rendered = compile_bytes(dir / "canonical.xml", render_toolchain_xml(model), false);
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    if (auto* failure = std::get_if<compiler::CompileFailure>(&original)) {
        for (const compiler::Diagnostic& d : failure->diagnostics) {
            if (d.severity == compiler::Severity::Error) r.diagnostics.push_back(convert(d, x));
        }
        return r;
    }
    const auto& orig = std::get<compiler::CompileResult>(original);
    for (const compiler::Diagnostic& d : orig.manifest.diagnostics) r.diagnostics.push_back(convert(d, x));
    const auto* canon = std::get_if<compiler::CompileResult>(&rendered);
    if (canon == nullptr || without_source_hash(canon->model) != without_source_hash(orig.model)) {
        r.diagnostics.push_back(Diagnostic{"error", "TWI002",
                                           "preservation check failed: the canonical model does not compile to the "
                                           "same IR as the original document",
                                           "this indicates an importer defect; the model is not imported",
                                           {"model", model.name, ""}, std::nullopt});
        return r;
    }

    Provenance p;
    p.original_filename = options.filename;
    p.original_sha256 = sha256_hex(xml);
    p.imported_at = now_utc_millis();
    p.importer = "uppaal-xml";
    p.importer_version = std::string(kImporterVersion);
    p.options = json::Json{{"legacySystemDeclaration", options.legacy_system_declaration}};
    p.content_sha256 = content_sha256(model);
    p.semantic_digest = semantic_digest(model);
    p.preserved = true;
    p.preservation_checks = 1 + orig.model.locations.size() + orig.model.transitions.size() +
                            orig.model.clocks.size() + orig.model.channels.size();
    r.provenance = std::move(p);
    r.model = std::move(model);
    r.layout = std::move(layout);
    return r;
}

}  // namespace twin::authoring
