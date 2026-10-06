/**
 * @file validate.cpp
 * @brief Structural validation of canonical models (codes TWM0xx, see validate.hpp).
 */
#include "twin/authoring/validate.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <queue>
#include <set>
#include <string>

namespace twin::authoring {
namespace {

/// UPPAAL keywords and built-in names (UTAP lexer), TwinTA keywords and the tau label.
const std::set<std::string, std::less<>>& reserved_words() {
    static const std::set<std::string, std::less<>> words = {
        // UPPAAL model-language keywords (UTAP lexer); property-language keywords such as
        // A, E, M, Pr, control, inf and sup are ordinary names inside models.
        "and", "assign", "bool", "branchpoint", "break", "broadcast", "case", "chan", "clock", "commit",
        "committed", "const", "continue", "deadlock", "default", "do", "double", "dynamic", "else", "exists",
        "exit", "false", "for", "forall", "guard", "hybrid", "if", "imply", "init", "int", "meta", "not", "numOf",
        "or", "priority", "probability", "process", "progress", "rate", "return", "scalar", "select", "spawn",
        "state", "string", "struct", "sum", "switch", "sync", "system", "trans", "true", "typedef", "urgent",
        "void", "while", "xor",
        "INT8_MIN", "INT8_MAX", "UINT8_MAX", "INT16_MIN", "INT16_MAX", "UINT16_MAX", "INT32_MIN", "INT32_MAX",
        "DBL_MIN", "DBL_MAX", "FLT_MIN", "FLT_MAX", "M_PI", "M_E",
        // TwinTA
        "automaton", "channel", "edge", "initial", "invariant", "location", "reset",
        // reserved label of the internal action
        "tau"};
    return words;
}

class Validator {
public:
    explicit Validator(const Model& m) : m_(m) {}

    std::vector<Diagnostic> run() {
        names();
        initial();
        edges();
        locations();
        reachability();
        unused();
        return std::move(out_);
    }

private:
    void add(std::string severity, std::string code, ElementRef element, std::string message, std::string hint = {}) {
        out_.push_back(Diagnostic{std::move(severity), std::move(code), std::move(message), std::move(hint),
                                  std::move(element), std::nullopt});
    }
    void error(std::string code, ElementRef element, std::string message, std::string hint = {}) {
        add("error", std::move(code), std::move(element), std::move(message), std::move(hint));
    }
    void warning(std::string code, ElementRef element, std::string message, std::string hint = {}) {
        add("warning", std::move(code), std::move(element), std::move(message), std::move(hint));
    }

    // ------------------------------------------------------------------ names
    void declare(const std::string& kind, const std::string& name) {
        if (!is_identifier(name)) {
            error("TWM001", {kind, name, "name"},
                  "'" + name + "' is not a valid name" +
                      (reserved_words().contains(name) ? " (it is a reserved word)" : ""),
                  "use letters, digits and '_', starting with a letter or '_', and avoid UPPAAL/TwinTA keywords");
            return;
        }
        const auto [it, inserted] = namespace_.emplace(name, kind);
        if (!inserted) {
            error("TWM002", {kind, name, "name"},
                  "'" + name + "' is already declared as a " + it->second,
                  "clocks, constants, channels, locations and the automaton name share one namespace");
        }
    }

    void names() {
        declare("model", m_.name);
        for (const ClockDecl& c : m_.clocks) {
            declare("clock", c.name);
            clocks_.insert(c.name);
        }
        for (const ConstantDecl& c : m_.constants) {
            declare("constant", c.name);
            constants_.emplace(c.name, c.value);
        }
        for (const ChannelDecl& c : m_.channels) {
            declare("channel", c.name);
            channels_.insert(c.name);
        }
        for (const LocationDecl& l : m_.locations) {
            declare("location", l.name);
            locations_.insert(l.name);
        }
    }

    void initial() {
        if (m_.locations.empty()) {
            error("TWM012", {"model", m_.name, ""}, "the automaton has no locations", "add at least one location");
            return;
        }
        bool seen = false;
        for (const LocationDecl& l : m_.locations) {
            if (!l.initial) continue;
            if (seen) {
                error("TWM004", {"location", l.name, ""}, "several locations are marked initial",
                      "exactly one location is initial");
            }
            seen = true;
        }
        if (!seen) {
            error("TWM003", {"model", m_.name, ""}, "no location is marked initial", "mark one location as initial");
        }
    }

    // ------------------------------------------------------------ constraints
    void constraint(const Constraint& c, const ElementRef& where, bool allow_diagonal) {
        for (const Atom& a : c) {
            clock_use(a.clock, where);
            if (a.minus) {
                clock_use(*a.minus, where);
                if (!allow_diagonal) {
                    error("TWM009", where, "diagonal constraint '" + a.clock + " - " + *a.minus + "' in a guard",
                          "the aligner's guard parser drops diagonal guards; diagonals are supported in invariants only");
                }
            }
            std::optional<std::int64_t> value;
            if (const auto* lit = std::get_if<std::int64_t>(&a.bound.value)) {
                value = *lit;
            } else {
                const std::string& name = std::get<std::string>(a.bound.value);
                used_constants_.insert(name);
                const auto it = constants_.find(name);
                if (it == constants_.end()) {
                    error("TWM007", where, "'" + name + "' is not a declared integer constant",
                          "declare it with 'const " + name + " = <value>;' or use a literal");
                } else {
                    value = it->second;
                }
            }
            if (value && std::llabs(*value) > max_bound()) {
                error("TWM010", where, "bound " + std::to_string(*value) + " is outside the supported range",
                      "UDBM represents bounds up to " + std::to_string(max_bound()) +
                          "; the aligner would read a larger bound as infinity");
            }
        }
    }

    void clock_use(const std::string& clock, const ElementRef& where) {
        used_clocks_.insert(clock);
        if (!clocks_.contains(clock)) {
            error("TWM006", where, "'" + clock + "' is not a declared clock", "declare it with 'clock " + clock + ";'");
        }
    }

    // ------------------------------------------------------------------ edges
    void edges() {
        std::set<std::string> ids;
        for (const EdgeDecl& e : m_.edges) {
            if (!is_edge_id(e.id)) {
                error("TWM011", {"edge", e.id, "name"}, "'" + e.id + "' is not a valid edge id",
                      "edge ids use letters, digits, '_', '.' and '-'");
            } else if (!ids.insert(e.id).second) {
                error("TWM011", {"edge", e.id, "name"}, "edge id '" + e.id + "' is used twice", "edge ids are unique");
            }
            for (const auto& [end, part] : {std::pair{&e.source, "source"}, std::pair{&e.target, "target"}}) {
                if (!locations_.contains(*end)) {
                    error("TWM005", {"edge", e.id, part}, "'" + *end + "' is not a declared location");
                }
            }
            if (e.sync) {
                used_channels_.insert(e.sync->channel);
                if (!channels_.contains(e.sync->channel)) {
                    error("TWM008", {"edge", e.id, "sync"}, "'" + e.sync->channel + "' is not a declared channel",
                          "declare it with 'channel " + e.sync->channel + ";'");
                }
                if (e.sync->direction == '?') {
                    warning("TWM024", {"edge", e.id, "sync"},
                            "receive label '" + e.sync->channel + "?' cannot be interpreted",
                            "interpretations map send labels 'a!'; the aligner neither matches nor explores receive "
                            "labels (alignment lint TWA011)");
                }
            }
            constraint(e.guard, {"edge", e.id, "guard"}, /*allow_diagonal=*/false);
            std::set<std::string> reset;
            for (const std::string& r : e.resets) {
                clock_use(r, {"edge", e.id, "reset"});
                if (!reset.insert(r).second) {
                    warning("TWM013", {"edge", e.id, "reset"}, "clock '" + r + "' is reset twice on this edge");
                }
            }
        }
    }

    void locations() {
        for (const LocationDecl& l : m_.locations) {
            constraint(l.invariant, {"location", l.name, "invariant"}, /*allow_diagonal=*/true);
        }
    }

    // ----------------------------------------------------------- reachability
    void reachability() {
        const LocationDecl* init = initial_location(m_);
        if (init == nullptr) return;
        std::map<std::string, std::vector<std::string>> succ;
        for (const EdgeDecl& e : m_.edges) succ[e.source].push_back(e.target);
        std::set<std::string> seen{init->name};
        std::queue<std::string> todo;
        todo.push(init->name);
        while (!todo.empty()) {
            const std::string l = todo.front();
            todo.pop();
            for (const std::string& t : succ[l]) {
                if (seen.insert(t).second) todo.push(t);
            }
        }
        for (const LocationDecl& l : m_.locations) {
            if (!seen.contains(l.name)) {
                warning("TWM023", {"location", l.name, ""},
                        "location '" + l.name + "' cannot be reached from the initial location by any edge",
                        "connect it, or remove it if it is not needed");
            }
        }
    }

    void unused() {
        for (const ClockDecl& c : m_.clocks) {
            if (!used_clocks_.contains(c.name)) warning("TWM020", {"clock", c.name, ""}, "clock '" + c.name + "' is never used");
        }
        for (const ChannelDecl& c : m_.channels) {
            if (!used_channels_.contains(c.name)) {
                warning("TWM021", {"channel", c.name, ""}, "channel '" + c.name + "' is never used");
            }
        }
        for (const ConstantDecl& c : m_.constants) {
            if (!used_constants_.contains(c.name)) {
                warning("TWM022", {"constant", c.name, ""}, "constant '" + c.name + "' is never used");
            }
        }
    }

    const Model& m_;
    std::vector<Diagnostic> out_;
    std::map<std::string, std::string> namespace_;
    std::set<std::string> clocks_, channels_, locations_;
    std::map<std::string, std::int64_t> constants_;
    std::set<std::string> used_clocks_, used_channels_, used_constants_;
};

}  // namespace

bool is_identifier(std::string_view name) {
    if (name.empty() || reserved_words().contains(name)) return false;
    const auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (!alpha(name.front())) return false;
    return std::all_of(name.begin(), name.end(), [&](char c) { return alpha(c) || digit(c); });
}

bool is_edge_id(std::string_view id) {
    if (id.empty()) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
               c == '-';
    });
}

std::int64_t max_bound() noexcept {
    // UDBM encodes bounds as (c << 1 | strictness) in int32 and reserves
    // dbm_INFINITY = INT_MAX >> 1; the compiler keeps a margin of one
    // (compiler::detail::max_constraint_constant; equality checked in tests).
    return (std::int64_t{2147483647} >> 1) - 1;
}

std::vector<Diagnostic> validate(const Model& model) { return Validator(model).run(); }

}  // namespace twin::authoring
