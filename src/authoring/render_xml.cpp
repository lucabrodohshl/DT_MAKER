/**
 * @file render_xml.cpp
 * @brief Deterministic UPPAAL XML renderings of canonical models (toolchain and exchange).
 */
#include <map>
#include <set>
#include <string>
#include <vector>

#include "twin/authoring/text.hpp"
#include "twin/authoring/uppaal.hpp"
#include "twin/core/sha256.hpp"

namespace twin::authoring {
namespace {

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string coords(const Point& p) { return " x=\"" + std::to_string(p.x) + "\" y=\"" + std::to_string(p.y) + "\""; }

/// Comma-separated names of @p decls.
template <class Decl>
std::string names(const std::vector<Decl>& decls) {
    std::string out;
    for (std::size_t i = 0; i < decls.size(); ++i) {
        if (i > 0) out += ", ";
        out += decls[i].name;
    }
    return out;
}

/// A note as `//` comment lines (exchange rendering only).
std::string comment_lines(const std::string& note) {
    std::string out;
    std::size_t start = 0;
    while (!note.empty()) {
        const std::size_t nl = note.find('\n', start);
        out += "// " + note.substr(start, nl == std::string::npos ? std::string::npos : nl - start) + "\n";
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

std::string declarations(const Model& m, bool exchange) {
    std::string out;
    if (!exchange) {
        if (!m.clocks.empty()) out += "clock " + names(m.clocks) + ";\n";
        for (const ConstantDecl& c : m.constants) out += "const int " + c.name + " = " + std::to_string(c.value) + ";\n";
        if (!m.channels.empty()) out += "chan " + names(m.channels) + ";\n";
        return out;
    }
    for (const ClockDecl& c : m.clocks) out += comment_lines(c.note) + "clock " + c.name + ";\n";
    for (const ConstantDecl& c : m.constants) {
        out += comment_lines(c.note) + "const int " + c.name + " = " + std::to_string(c.value) + ";\n";
    }
    for (const ChannelDecl& c : m.channels) out += comment_lines(c.note) + "chan " + c.name + ";\n";
    return out;
}

/// The process name of the generated system line: "P", made unique against the model's names.
std::string process_name(const Model& m) {
    std::set<std::string> taken{m.name};
    for (const ClockDecl& c : m.clocks) taken.insert(c.name);
    for (const ConstantDecl& c : m.constants) taken.insert(c.name);
    for (const ChannelDecl& c : m.channels) taken.insert(c.name);
    std::string p = "P";
    while (taken.contains(p)) p += "_";
    return p;
}

std::string render(const Model& m, const Layout* layout) {
    const bool exchange = layout != nullptr;
    // Initial location first (the aligner starts from the first location), others in order.
    std::vector<const LocationDecl*> order;
    for (const LocationDecl& l : m.locations) {
        if (l.initial) order.insert(order.begin(), &l);
        else order.push_back(&l);
    }
    std::map<std::string, std::string> ids;
    for (std::size_t i = 0; i < order.size(); ++i) ids[order[i]->name] = "id" + std::to_string(i);

    std::string out =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<!DOCTYPE nta PUBLIC '-//Uppaal Team//DTD Flat System 1.6//EN' "
        "'http://www.it.uu.se/research/group/darts/uppaal/flat-1_6.dtd'>\n";
    if (exchange && !m.note.empty()) {
        std::string note = m.note;
        for (std::size_t p = note.find("--"); p != std::string::npos; p = note.find("--", p)) note.replace(p, 2, "- -");
        out += "<!-- " + note + " -->\n";
    }
    out += "<nta>\n<declaration>" + escape(declarations(m, exchange)) + "</declaration>\n";
    out += "<template>\n<name>" + escape(m.name) + "</name>\n<declaration></declaration>\n";
    for (const LocationDecl* l : order) {
        out += "<location id=\"" + ids[l->name] + "\"";
        const LocationLayout* ll = nullptr;
        if (exchange) {
            const auto it = layout->locations.find(l->name);
            if (it != layout->locations.end()) ll = &it->second;
        }
        if (ll != nullptr) out += coords(ll->position);
        out += "><name";
        if (ll != nullptr && ll->label) out += coords(*ll->label);
        out += ">" + escape(l->name) + "</name>";
        if (!l->invariant.empty()) out += "<label kind=\"invariant\">" + escape(constraint_text(l->invariant)) + "</label>";
        if (exchange && !l->note.empty()) out += "<label kind=\"comments\">" + escape(l->note) + "</label>";
        out += "</location>\n";
    }
    if (!order.empty()) out += "<init ref=\"" + ids[order.front()->name] + "\"/>\n";
    for (const EdgeDecl& e : m.edges) {
        out += "<transition><source ref=\"" + ids[e.source] + "\"/><target ref=\"" + ids[e.target] + "\"/>";
        const EdgeLayout* el = nullptr;
        if (exchange) {
            const auto it = layout->edges.find(e.id);
            if (it != layout->edges.end()) el = &it->second;
        }
        std::int64_t dy = 0;
        const auto label = [&](const char* kind, const std::string& text) {
            out += std::string("<label kind=\"") + kind + "\"";
            if (el != nullptr && el->label) out += coords(Point{el->label->x, el->label->y + dy});
            dy += 17;
            out += ">" + escape(text) + "</label>";
        };
        if (!e.guard.empty()) label("guard", constraint_text(e.guard));
        if (e.sync) label("synchronisation", e.sync->channel + std::string(1, e.sync->direction));
        if (!e.resets.empty()) {
            std::string a;
            for (std::size_t i = 0; i < e.resets.size(); ++i) a += (i > 0 ? ", " : "") + e.resets[i] + " := 0";
            label("assignment", a);
        }
        if (exchange && !e.note.empty()) label("comments", e.note);
        if (el != nullptr) {
            for (const Point& p : el->nails) out += "<nail" + coords(p) + "/>";
        }
        out += "</transition>\n";
    }
    const std::string process = process_name(m);
    out += "</template>\n<system>" + process + " = " + escape(m.name) + "();\nsystem " + process + ";</system>\n</nta>\n";
    return out;
}

}  // namespace

std::string render_toolchain_xml(const Model& model) { return render(model, nullptr); }

std::string render_exchange_xml(const Model& model, const Layout& layout) { return render(model, &layout); }

std::string semantic_digest(const Model& model) { return sha256_hex(render_toolchain_xml(model)); }

}  // namespace twin::authoring
