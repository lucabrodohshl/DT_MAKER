/**
 * @file validate.cpp
 * @brief Structural well-formedness rules of the IR.
 */
#include "twin/ir/validate.hpp"

#include <algorithm>
#include <set>

namespace twin::ir {
namespace {

bool is_ident_start(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool is_ident_char(char c) noexcept { return is_ident_start(c) || (c >= '0' && c <= '9'); }

/// Model ids/versions: [A-Za-z0-9._-]+
bool is_token(std::string_view text) noexcept {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return is_ident_char(c) || c == '.' || c == '-';
    });
}

/// Transition ids: identifiers plus the separators used by the compiler's id scheme.
bool is_transition_id(std::string_view text) noexcept {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return is_ident_char(c) || c == '.' || c == '!' || c == '?' || c == '#' || c == '-';
    });
}

class Validator {
public:
    explicit Validator(const Model& m) : m_(m) {}

    std::vector<Error> run() {
        check_info();
        check_clocks();
        check_channels();
        check_locations();
        check_transitions();
        check_propositions();
        check_event_interpretations();
        return std::move(errors_);
    }

private:
    void fail(std::string message, std::string key = {}, std::string value = {}) {
        Error e = make_error(ErrorCode::ValidationError, std::move(message));
        if (!key.empty()) {
            e.with(std::move(key), std::move(value));
        }
        errors_.push_back(std::move(e));
    }

    void check_info() {
        if (!is_token(m_.info.id)) fail("model id must match [A-Za-z0-9._-]+", "id", m_.info.id);
        if (!is_token(m_.info.version))
            fail("model version must match [A-Za-z0-9._-]+", "version", m_.info.version);
        if (m_.info.source_template.empty()) fail("source template name is empty");
        if (!m_.info.source_sha256.empty() && m_.info.source_sha256.size() != 64)
            fail("source_sha256 must be a 64-character hex digest");
        if (Status s = validate_time_base(m_.time); !s) errors_.push_back(s.error());
    }

    void check_clocks() {
        std::set<std::string_view> seen;
        for (const std::string& c : m_.clocks) {
            if (!is_identifier(c)) fail("clock name is not an identifier", "clock", c);
            if (!seen.insert(c).second) fail("duplicate clock", "clock", c);
        }
    }

    void check_channels() {
        if (!std::is_sorted(m_.channels.begin(), m_.channels.end()))
            fail("channels must be sorted");
        std::set<std::string_view> seen;
        for (const std::string& c : m_.channels) {
            if (!is_identifier(c)) fail("channel name is not an identifier", "channel", c);
            if (!seen.insert(c).second) fail("duplicate channel", "channel", c);
        }
    }

    void check_constraint(const ClockConstraint& c, const std::string& where) {
        const auto n = static_cast<ClockIndex>(m_.clocks.size());
        if (c.lhs == kReferenceClock || c.lhs > n)
            fail("constraint refers to an unknown clock", "where", where);
        if (c.rhs > n) fail("constraint refers to an unknown clock", "where", where);
        if (c.lhs == c.rhs) fail("constraint compares a clock with itself", "where", where);
        if (c.bound > kMaxModelConstant || c.bound < -kMaxModelConstant)
            fail("constraint bound out of range", "where", where);
    }

    void check_conjunction(const Conjunction& g, const std::string& where) {
        for (const ClockConstraint& c : g) check_constraint(c, where);
        Conjunction sorted = g;
        canonicalize(sorted);
        if (sorted != g) fail("conjunction is not in canonical (sorted, unique) form", "where", where);
    }

    void check_locations() {
        if (m_.locations.empty()) fail("a model needs at least one location");
        std::set<std::string_view> seen;
        for (const Location& l : m_.locations) {
            if (!is_identifier(l.id)) fail("location id is not an identifier", "location", l.id);
            if (!seen.insert(l.id).second) fail("duplicate location", "location", l.id);
            check_conjunction(l.invariant, "invariant of " + l.id);
        }
        if (m_.initial >= m_.locations.size()) fail("initial location out of range");
    }

    void check_transitions() {
        std::set<std::string_view> seen;
        const auto n_loc = m_.locations.size();
        const auto n_clk = static_cast<ClockIndex>(m_.clocks.size());
        for (const Transition& t : m_.transitions) {
            if (!is_transition_id(t.id)) fail("malformed transition id", "transition", t.id);
            if (!seen.insert(t.id).second) fail("duplicate transition id", "transition", t.id);
            if (t.source >= n_loc || t.target >= n_loc)
                fail("transition endpoint out of range", "transition", t.id);
            if (t.action.kind == ActionKind::Internal) {
                if (!t.action.channel.empty())
                    fail("internal action must not name a channel", "transition", t.id);
            } else if (!std::binary_search(m_.channels.begin(), m_.channels.end(),
                                           t.action.channel)) {
                fail("transition uses an undeclared channel", "transition", t.id);
            }
            check_conjunction(t.guard, "guard of " + t.id);
            if (!std::is_sorted(t.resets.begin(), t.resets.end()) ||
                std::adjacent_find(t.resets.begin(), t.resets.end()) != t.resets.end())
                fail("resets must be sorted and unique", "transition", t.id);
            for (ClockIndex r : t.resets) {
                if (r == kReferenceClock || r > n_clk)
                    fail("reset of an unknown clock", "transition", t.id);
            }
        }
    }

    void check_propositions() {
        if (m_.propositions.size() != m_.locations.size()) {
            fail("there must be exactly one proposition per location");
            return;
        }
        for (std::size_t i = 0; i < m_.propositions.size(); ++i) {
            const Proposition& p = m_.propositions[i];
            if (p.location != i) fail("proposition order must follow location order", "proposition", p.id);
            if (p.location < m_.locations.size() &&
                p.id != "at(" + m_.locations[p.location].id + ")")
                fail("proposition id must be at(<location>)", "proposition", p.id);
        }
    }

    void check_event_interpretations() {
        std::set<std::string> labels;
        for (const Transition& t : m_.transitions) {
            if (t.action.kind != ActionKind::Internal) labels.insert(t.action.label());
        }
        for (std::size_t i = 0; i < m_.event_interpretations.size(); ++i) {
            const EventInterpretation& e = m_.event_interpretations[i];
            if (i > 0 && !(m_.event_interpretations[i - 1].label < e.label))
                fail("event interpretations must be sorted by label and unique", "label", e.label);
            if (labels.count(e.label) == 0)
                fail("event interpretation for a label no transition carries", "label", e.label);
            if (e.formula.empty()) fail("empty event interpretation", "label", e.label);
        }
    }

    const Model& m_;
    std::vector<Error> errors_;
};

}  // namespace

bool is_identifier(std::string_view text) noexcept {
    return !text.empty() && is_ident_start(text.front()) &&
           std::all_of(text.begin(), text.end(), is_ident_char);
}

std::vector<Error> validate_all(const Model& model) { return Validator(model).run(); }

Status validate(const Model& model) {
    std::vector<Error> errors = validate_all(model);
    if (errors.empty()) {
        return ok_status();
    }
    return std::move(errors.front());
}

}  // namespace twin::ir
