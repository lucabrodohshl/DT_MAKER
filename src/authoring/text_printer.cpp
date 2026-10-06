/**
 * @file text_printer.cpp
 * @brief Canonical TwinTA printing (notes become `//` lines before their declaration).
 */
#include <string>
#include <vector>

#include "twin/authoring/text.hpp"
#include "twin/authoring/validate.hpp"

namespace twin::authoring {
namespace {

constexpr const char* kIndent = "    ";

void note_lines(std::string& out, const std::string& note, const char* indent) {
    if (note.empty()) return;
    std::size_t start = 0;
    while (true) {
        const std::size_t nl = note.find('\n', start);
        const std::string line = note.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        out += indent;
        out += line.empty() ? std::string("//") : "// " + line;
        out += '\n';
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
}

std::string bound_text(const Bound& b) {
    if (const auto* v = std::get_if<std::int64_t>(&b.value)) return std::to_string(*v);
    return std::get<std::string>(b.value);
}

std::string edge_id_text(const std::string& id) {
    if (is_identifier(id)) return id;
    std::string out = "\"";
    for (char c : id) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

/// `keyword a, b;` for runs of declarations without notes; one line each for annotated ones.
template <class Decl>
void grouped(std::string& out, const char* keyword, const std::vector<Decl>& decls) {
    std::vector<std::string> run;
    const auto flush = [&] {
        if (run.empty()) return;
        out += kIndent;
        out += keyword;
        out += ' ';
        for (std::size_t i = 0; i < run.size(); ++i) {
            if (i > 0) out += ", ";
            out += run[i];
        }
        out += ";\n";
        run.clear();
    };
    for (const Decl& d : decls) {
        if (d.note.empty()) {
            run.push_back(d.name);
            continue;
        }
        flush();
        note_lines(out, d.note, kIndent);
        run.push_back(d.name);
        flush();
    }
    flush();
}

}  // namespace

std::string constraint_text(const Constraint& c) {
    if (c.empty()) return "true";
    std::string out;
    for (std::size_t i = 0; i < c.size(); ++i) {
        if (i > 0) out += " && ";
        out += c[i].clock;
        if (c[i].minus) out += " - " + *c[i].minus;
        out += ' ';
        out += ir::to_string(c[i].op);
        out += ' ';
        out += bound_text(c[i].bound);
    }
    return out;
}

namespace {

/// Clocks, constants and channels; true if anything was printed.
bool print_declarations(std::string& out, const Model& m) {
    grouped(out, "clock", m.clocks);
    for (const ConstantDecl& c : m.constants) {
        note_lines(out, c.note, kIndent);
        out += std::string(kIndent) + "const " + c.name + " = " + std::to_string(c.value) + ";\n";
    }
    grouped(out, "channel", m.channels);
    return !m.clocks.empty() || !m.constants.empty() || !m.channels.empty();
}

void print_locations(std::string& out, const Model& m) {
    for (const LocationDecl& l : m.locations) {
        note_lines(out, l.note, kIndent);
        out += kIndent;
        if (l.initial) out += "initial ";
        out += "location " + l.name;
        out += l.invariant.empty() ? ";\n" : " { invariant " + constraint_text(l.invariant) + "; }\n";
    }
}

std::string edge_items(const EdgeDecl& e) {
    std::string items;
    if (!e.guard.empty()) items += "guard " + constraint_text(e.guard) + "; ";
    if (e.sync) items += "sync " + e.sync->channel + std::string(1, e.sync->direction) + "; ";
    if (!e.resets.empty()) {
        items += "reset ";
        for (std::size_t i = 0; i < e.resets.size(); ++i) {
            if (i > 0) items += ", ";
            items += e.resets[i];
        }
        items += "; ";
    }
    return items;
}

void print_edges(std::string& out, const Model& m) {
    for (const EdgeDecl& e : m.edges) {
        note_lines(out, e.note, kIndent);
        const std::string items = edge_items(e);
        out += std::string(kIndent) + "edge " + edge_id_text(e.id) + ": " + e.source + " -> " + e.target;
        out += items.empty() ? ";\n" : " { " + items + "}\n";
    }
}

}  // namespace

std::string print_text(const Model& m) {
    std::string out;
    note_lines(out, m.note, "");
    out += "automaton " + m.name + " {\n";
    bool section = print_declarations(out, m);
    if (!m.locations.empty()) {
        if (section) out += '\n';
        print_locations(out, m);
        section = true;
    }
    if (!m.edges.empty()) {
        if (section) out += '\n';
        print_edges(out, m);
    }
    out += "}\n";
    return out;
}

}  // namespace twin::authoring
