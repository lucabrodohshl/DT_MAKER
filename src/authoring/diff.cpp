/**
 * @file diff.cpp
 * @brief Structural model diff (declarations by name, edges by id).
 */
#include "twin/authoring/diff.hpp"

#include <map>
#include <set>

#include "twin/authoring/text.hpp"
#include "twin/authoring/uppaal.hpp"

namespace twin::authoring {
namespace {

json::Json from_to(const json::Json& a, const json::Json& b) { return json::Json{{"from", a}, {"to", b}}; }

template <class Decl>
json::Json added_removed(const std::vector<Decl>& a, const std::vector<Decl>& b) {
    std::set<std::string> na;
    std::set<std::string> nb;
    for (const Decl& d : a) na.insert(d.name);
    for (const Decl& d : b) nb.insert(d.name);
    json::Json added = json::Json::array();
    json::Json removed = json::Json::array();
    for (const Decl& d : b) {
        if (!na.contains(d.name)) added.push_back(d.name);
    }
    for (const Decl& d : a) {
        if (!nb.contains(d.name)) removed.push_back(d.name);
    }
    return json::Json{{"added", added}, {"removed", removed}};
}

void note_change(json::Json& notes, const std::string& kind, const std::string& name, const std::string& a,
                 const std::string& b) {
    if (a != b) notes.push_back({{"element", {{"kind", kind}, {"name", name}}}, {"from", a}, {"to", b}});
}

std::string sync_text(const std::optional<Sync>& s) {
    return s ? s->channel + std::string(1, s->direction) : std::string("tau");
}

std::string resets_text(const std::vector<std::string>& r) {
    std::string out;
    for (std::size_t i = 0; i < r.size(); ++i) out += (i > 0 ? ", " : "") + r[i];
    return out;
}

}  // namespace

json::Json diff_models(const Model& from, const Model& to) {
    json::Json notes = json::Json::array();
    note_change(notes, "model", to.name, from.note, to.note);
    json::Json d{{"semanticChange", semantic_digest(from) != semantic_digest(to)},
                 {"digests", from_to(semantic_digest(from), semantic_digest(to))},
                 {"clocks", added_removed(from.clocks, to.clocks)},
                 {"channels", added_removed(from.channels, to.channels)}};
    if (from.name != to.name) d["name"] = from_to(from.name, to.name);

    for (const auto& [a, b] : {std::pair{&from.clocks, &to.clocks}}) {
        for (const ClockDecl& x : *a) {
            for (const ClockDecl& y : *b) {
                if (x.name == y.name) note_change(notes, "clock", x.name, x.note, y.note);
            }
        }
    }
    for (const ChannelDecl& x : from.channels) {
        for (const ChannelDecl& y : to.channels) {
            if (x.name == y.name) note_change(notes, "channel", x.name, x.note, y.note);
        }
    }

    json::Json constants = added_removed(from.constants, to.constants);
    constants["changed"] = json::Json::array();
    for (const ConstantDecl& x : from.constants) {
        for (const ConstantDecl& y : to.constants) {
            if (x.name != y.name) continue;
            if (x.value != y.value) constants["changed"].push_back({{"name", x.name}, {"from", x.value}, {"to", y.value}});
            note_change(notes, "constant", x.name, x.note, y.note);
        }
    }
    d["constants"] = constants;

    json::Json locations = added_removed(from.locations, to.locations);
    locations["changed"] = json::Json::array();
    for (const LocationDecl& x : from.locations) {
        for (const LocationDecl& y : to.locations) {
            if (x.name != y.name) continue;
            json::Json c{{"name", x.name}};
            if (x.initial != y.initial) c["initial"] = from_to(x.initial, y.initial);
            if (x.invariant != y.invariant) {
                c["invariant"] = from_to(constraint_text(x.invariant), constraint_text(y.invariant));
            }
            if (c.size() > 1) locations["changed"].push_back(c);
            note_change(notes, "location", x.name, x.note, y.note);
        }
    }
    d["locations"] = locations;

    std::map<std::string, const EdgeDecl*> before;
    std::map<std::string, const EdgeDecl*> after;
    for (const EdgeDecl& e : from.edges) before[e.id] = &e;
    for (const EdgeDecl& e : to.edges) after[e.id] = &e;
    json::Json edges{{"added", json::Json::array()}, {"removed", json::Json::array()}, {"changed", json::Json::array()}};
    for (const EdgeDecl& e : to.edges) {
        if (!before.contains(e.id)) edges["added"].push_back(e.id);
    }
    for (const EdgeDecl& e : from.edges) {
        const auto it = after.find(e.id);
        if (it == after.end()) {
            edges["removed"].push_back(e.id);
            continue;
        }
        const EdgeDecl& y = *it->second;
        json::Json fields = json::Json::object();
        if (e.source != y.source) fields["source"] = from_to(e.source, y.source);
        if (e.target != y.target) fields["target"] = from_to(e.target, y.target);
        if (e.sync != y.sync) fields["sync"] = from_to(sync_text(e.sync), sync_text(y.sync));
        if (e.guard != y.guard) fields["guard"] = from_to(constraint_text(e.guard), constraint_text(y.guard));
        if (e.resets != y.resets) fields["resets"] = from_to(resets_text(e.resets), resets_text(y.resets));
        if (!fields.empty()) edges["changed"].push_back({{"id", e.id}, {"fields", fields}});
        note_change(notes, "edge", e.id, e.note, y.note);
    }
    d["edges"] = edges;
    d["notes"] = notes;
    return d;
}

}  // namespace twin::authoring
