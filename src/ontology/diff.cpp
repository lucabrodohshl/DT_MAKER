/**
 * @file diff.cpp
 * @brief Structural diff (see diff.hpp).
 */
#include "twin/ontology/diff.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace twin::ontology {

const char* to_string(ChangeKind kind) noexcept {
    switch (kind) {
        case ChangeKind::Added: return "added";
        case ChangeKind::Removed: return "removed";
        case ChangeKind::Modified: return "modified";
        case ChangeKind::Renamed: return "renamed";
    }
    return "modified";
}

std::string signature(const FunctionDecl& f) {
    std::string s;
    for (const auto& a : f.arg_sorts) s += a + " ";
    if (!f.arg_sorts.empty()) s += "-> ";
    return s + f.return_sort;
}

std::string signature(const RelationDecl& r) {
    std::string s;
    for (const auto& a : r.arg_sorts) s += (s.empty() ? "" : " ") + a;
    return s;
}

std::string normalize_formula(std::string_view formula) {
    std::string out;
    bool space = false;
    for (char c : formula) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            space = true;
            continue;
        }
        // No space directly inside parentheses: "( and a )" == "(and a)".
        if (space && !out.empty() && out.back() != '(' && c != ')') out += ' ';
        space = false;
        out += c;
    }
    return out;
}

namespace {

void add_symbols_of(const std::string& formula, std::set<std::string>& into) {
    if (auto syms = formula_symbols(formula)) into.insert(syms->begin(), syms->end());
}

/// @brief Generic keyed diff: items are (name, comparable rendering).
void keyed_diff(const std::string& element, const std::vector<std::pair<std::string, std::string>>& from,
                const std::vector<std::pair<std::string, std::string>>& to, bool detect_renames,
                std::vector<StructuralChange>& out) {
    std::map<std::string, std::string> old_items(from.begin(), from.end());
    std::map<std::string, std::string> new_items(to.begin(), to.end());
    std::vector<StructuralChange> removed;
    std::vector<StructuralChange> added;
    for (const auto& [name, value] : from) {
        auto it = new_items.find(name);
        if (it == new_items.end()) {
            removed.push_back({ChangeKind::Removed, element, name, {}, value, {}});
        } else if (it->second != value) {
            out.push_back({ChangeKind::Modified, element, name, {}, value, it->second});
        }
    }
    for (const auto& [name, value] : to) {
        if (old_items.count(name) == 0) added.push_back({ChangeKind::Added, element, name, {}, {}, value});
    }
    if (detect_renames) {
        // An id that disappeared while an identical formula appeared under a new id is a rename.
        for (auto r = removed.begin(); r != removed.end();) {
            auto a = std::find_if(added.begin(), added.end(), [&](const StructuralChange& c) { return c.after == r->before; });
            if (a != added.end()) {
                out.push_back({ChangeKind::Renamed, element, a->name, r->name, r->before, a->after});
                added.erase(a);
                r = removed.erase(r);
            } else {
                ++r;
            }
        }
    }
    out.insert(out.end(), removed.begin(), removed.end());
    out.insert(out.end(), added.begin(), added.end());
}

}  // namespace

StructuralDiff diff_ontologies(const OntologySource& from, const OntologySource& to) {
    StructuralDiff d;
    auto sorts = [](const OntologySource& s) {
        std::vector<std::pair<std::string, std::string>> v;
        for (const auto& x : s.sorts) v.emplace_back(x.name, "");
        return v;
    };
    auto funcs = [](const OntologySource& s) {
        std::vector<std::pair<std::string, std::string>> v;
        for (const auto& x : s.functions) v.emplace_back(x.name, signature(x));
        return v;
    };
    auto rels = [](const OntologySource& s) {
        std::vector<std::pair<std::string, std::string>> v;
        for (const auto& x : s.relations) v.emplace_back(x.name, signature(x));
        return v;
    };
    auto axioms = [](const OntologySource& s) {
        std::vector<std::pair<std::string, std::string>> v;
        for (const auto& x : s.axioms) v.emplace_back(x.id, normalize_formula(x.formula));
        return v;
    };
    keyed_diff("sort", sorts(from), sorts(to), false, d.changes);
    keyed_diff("function", funcs(from), funcs(to), false, d.changes);
    keyed_diff("relation", rels(from), rels(to), false, d.changes);
    keyed_diff("axiom", axioms(from), axioms(to), true, d.changes);

    for (const auto& c : d.changes) {
        if (c.element == "axiom") {
            if (c.kind == ChangeKind::Renamed) continue;  // same formula: no meaning change
            add_symbols_of(c.before, d.affected_symbols);
            add_symbols_of(c.after, d.affected_symbols);
        } else if (c.element == "function" || c.element == "relation") {
            d.affected_symbols.insert(c.name);
        }
    }
    return d;
}

StructuralDiff diff_interpretations(const InterpretationSource& from, const InterpretationSource& to) {
    StructuralDiff d;
    auto entries = [](const InterpretationSource& s, bool events) {
        std::vector<std::pair<std::string, std::string>> v;
        for (const auto& e : s.entries) {
            if (e.is_event == events) v.emplace_back(e.key, normalize_formula(e.formula));
        }
        return v;
    };
    keyed_diff("location", entries(from, false), entries(to, false), false, d.changes);
    keyed_diff("event", entries(from, true), entries(to, true), false, d.changes);
    for (const auto& c : d.changes) {
        add_symbols_of(c.before, d.affected_symbols);
        add_symbols_of(c.after, d.affected_symbols);
    }
    return d;
}

std::vector<std::string> entries_using(const InterpretationSource& interpretation, const std::set<std::string>& symbols) {
    std::vector<std::string> out;
    for (const auto& e : interpretation.entries) {
        const auto syms = formula_symbols(e.formula);
        if (!syms) continue;
        if (std::any_of(syms->begin(), syms->end(), [&](const std::string& s) { return symbols.count(s) != 0; })) {
            out.push_back(e.key);
        }
    }
    return out;
}

}  // namespace twin::ontology
