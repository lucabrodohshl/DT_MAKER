/**
 * @file property.cpp
 * @brief Property language: tokenizer, recursive-descent parser, printer, state-set evaluation.
 */
#include "twin/monitoring/property.hpp"

#include <cctype>
#include <limits>

namespace twin::monitoring {
namespace {

using Kind = StateFormula::Kind;

Error parse_error(const std::string& message, std::size_t column) {
    return make_error(ErrorCode::ParseError, message).with("column", std::to_string(column));
}

/// A minimal tokenizer over the property text (positions are 1-based columns).
class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    Result<Property> run() {
        skip();
        Property p;
        static constexpr std::pair<std::string_view, Quantifier> kQuantifiers[] = {
            {"A[]", Quantifier::Always}, {"E<>", Quantifier::Eventually},
            {"A<>", Quantifier::Inevitably}, {"E[]", Quantifier::Potentially}};
        bool quantified = false;
        for (const auto& [text, q] : kQuantifiers) {
            if (s_.substr(i_, text.size()) == text) {
                i_ += text.size();
                p.quantifier = q;
                quantified = true;
                break;
            }
        }
        Result<StateFormula> phi = state();
        if (!phi) return std::move(phi).error();
        p.phi = std::move(phi).value();
        skip();
        if (!quantified) {
            if (s_.substr(i_, 3) != "-->") {
                return parse_error("expected a quantifier (A[], E<>, A<>, E[]) or 'p --> q'", 1);
            }
            i_ += 3;
            p.quantifier = Quantifier::LeadsTo;
            Result<StateFormula> psi = state();
            if (!psi) return std::move(psi).error();
            p.psi = std::move(psi).value();
            skip();
        }
        if (i_ < s_.size()) return parse_error("unexpected '" + std::string(s_.substr(i_, 1)) + "'", i_ + 1);
        return p;
    }

private:
    void skip() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_])) != 0) ++i_;
    }
    [[nodiscard]] bool at(std::string_view t) {
        skip();
        return s_.substr(i_, t.size()) == t;
    }
    /// Consume @p t if present; "-" must not start "->" or "-->".
    bool accept(std::string_view t) {
        if (!at(t)) return false;
        if (t == "-" && (s_.substr(i_, 2) == "->" || s_.substr(i_, 2) == "--")) return false;
        if (t == "->" && (s_.substr(i_, 3) == "-->")) return false;
        i_ += t.size();
        return true;
    }
    bool accept_word(std::string_view w) {
        skip();
        if (s_.substr(i_, w.size()) != w) return false;
        const std::size_t e = i_ + w.size();
        if (e < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[e])) != 0 || s_[e] == '_')) return false;
        i_ = e;
        return true;
    }
    std::string ident() {
        skip();
        const std::size_t b = i_;
        if (i_ < s_.size() && (std::isalpha(static_cast<unsigned char>(s_[i_])) != 0 || s_[i_] == '_')) {
            ++i_;
            while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) != 0 || s_[i_] == '_')) ++i_;
        }
        return std::string(s_.substr(b, i_ - b));
    }

    Result<std::int64_t> integer() {
        skip();
        const std::size_t start = i_;
        const bool negative = accept("-");
        skip();
        const std::size_t b = i_;
        while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_])) != 0) ++i_;
        if (b == i_) return parse_error("expected an integer bound", start + 1);
        if (i_ < s_.size() && s_[i_] == '.') return parse_error("clock bounds are integers (model time units)", start + 1);
        try {
            const long long v = std::stoll(std::string(s_.substr(b, i_ - b)));
            return negative ? -static_cast<std::int64_t>(v) : static_cast<std::int64_t>(v);
        } catch (const std::exception&) {
            return parse_error("integer out of range", start + 1);
        }
    }

    std::optional<ir::Comparison> comparison() {
        for (const char* op : {"<=", ">=", "==", "<", ">"}) {
            if (accept(op)) return ir::parse_comparison(op);
        }
        return std::nullopt;
    }

    static ir::Comparison mirror(ir::Comparison op) {
        switch (op) {
            case ir::Comparison::Less: return ir::Comparison::Greater;
            case ir::Comparison::LessEqual: return ir::Comparison::GreaterEqual;
            case ir::Comparison::Equal: return ir::Comparison::Equal;
            case ir::Comparison::GreaterEqual: return ir::Comparison::LessEqual;
            case ir::Comparison::Greater: return ir::Comparison::Less;
        }
        return op;
    }

    static StateFormula node(Kind k, std::vector<StateFormula> children) {
        StateFormula f;
        f.kind = k;
        f.children = std::move(children);
        return f;
    }

    // state = or [ "->" state ]
    Result<StateFormula> state() {
        Result<StateFormula> lhs = disjunction();
        if (!lhs) return lhs;
        if (accept("->") || accept_word("imply")) {
            Result<StateFormula> rhs = state();
            if (!rhs) return rhs;
            return node(Kind::Implies, {std::move(lhs).value(), std::move(rhs).value()});
        }
        return lhs;
    }

    Result<StateFormula> disjunction() {
        Result<StateFormula> lhs = conjunction();
        if (!lhs) return lhs;
        StateFormula f = std::move(lhs).value();
        while (accept("||") || accept_word("or")) {
            Result<StateFormula> rhs = conjunction();
            if (!rhs) return rhs;
            f = node(Kind::Or, {std::move(f), std::move(rhs).value()});
        }
        return f;
    }

    Result<StateFormula> conjunction() {
        Result<StateFormula> lhs = unary();
        if (!lhs) return lhs;
        StateFormula f = std::move(lhs).value();
        while (accept("&&") || accept_word("and")) {
            Result<StateFormula> rhs = unary();
            if (!rhs) return rhs;
            f = node(Kind::And, {std::move(f), std::move(rhs).value()});
        }
        return f;
    }

    Result<StateFormula> unary() {
        if (at("!=")) return parse_error("unexpected '!='", i_ + 1);
        if (accept("!") || accept_word("not")) {
            Result<StateFormula> inner = unary();
            if (!inner) return inner;
            return node(Kind::Not, {std::move(inner).value()});
        }
        if (accept("(")) {
            const std::size_t open = i_;
            Result<StateFormula> inner = state();
            if (!inner) return inner;
            if (!accept(")")) return parse_error("missing ')'", open);
            return inner;
        }
        return atom();
    }

    Result<StateFormula> semantic() {
        // after "sem(": the SMT-LIB2 text up to the matching ')'
        const std::size_t b = i_;
        int depth = 1;
        while (i_ < s_.size() && depth > 0) {
            if (s_[i_] == '(') ++depth;
            if (s_[i_] == ')') --depth;
            ++i_;
        }
        if (depth != 0) return parse_error("unterminated sem( ... )", b);
        StateFormula f;
        f.kind = Kind::Semantic;
        std::string text(s_.substr(b, i_ - 1 - b));
        const auto first = text.find_first_not_of(" \t");
        const auto last = text.find_last_not_of(" \t");
        f.formula = first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
        if (f.formula.empty()) return parse_error("empty sem()", b);
        return f;
    }

    Result<StateFormula> atom() {
        skip();
        const std::size_t col = i_ + 1;
        if (i_ >= s_.size()) return parse_error("expected a state formula", col);
        if (accept_word("true")) return node(Kind::True, {});
        if (accept_word("false")) return node(Kind::False, {});
        if (s_.substr(i_, 4) == "sem(") {
            i_ += 4;
            return semantic();
        }
        if (std::isdigit(static_cast<unsigned char>(s_[i_])) != 0 || (s_[i_] == '-' && i_ + 1 < s_.size() &&
                                                                       std::isdigit(static_cast<unsigned char>(s_[i_ + 1])) != 0)) {
            Result<std::int64_t> bound = integer();
            if (!bound) return std::move(bound).error();
            const std::optional<ir::Comparison> op = comparison();
            if (!op) return parse_error("expected a comparison after the bound", i_ + 1);
            StateFormula f;
            f.kind = Kind::Clock;
            f.name = ident();
            if (f.name.empty()) return parse_error("expected a clock", i_ + 1);
            f.op = mirror(*op);
            f.bound = bound.value();
            return f;
        }
        std::string name = ident();
        if (name.empty()) return parse_error("unexpected '" + std::string(s_.substr(i_, 1)) + "'", col);
        if (s_.substr(i_, 1) == "." ) {  // Process.Location
            ++i_;
            name = ident();
            if (name.empty()) return parse_error("expected a location after '.'", i_ + 1);
            StateFormula f;
            f.kind = Kind::Location;
            f.name = name;
            return f;
        }
        StateFormula f;
        f.name = name;
        skip();
        // a diagonal "x - y", not the start of "->" or "-->"
        const bool diagonal = i_ + 1 < s_.size() && s_[i_] == '-' && s_[i_ + 1] != '-' && s_[i_ + 1] != '>';
        if (diagonal) {
            ++i_;
            f.minus = ident();
            if (f.minus->empty()) return parse_error("expected a clock after '-'", i_ + 1);
        }
        if (const std::optional<ir::Comparison> op = comparison()) {
            f.kind = Kind::Clock;
            f.op = *op;
            skip();
            if (i_ < s_.size() && (std::isalpha(static_cast<unsigned char>(s_[i_])) != 0 || s_[i_] == '_')) {
                return parse_error("clock bounds are integers (model time units)", i_ + 1);
            }
            Result<std::int64_t> bound = integer();
            if (!bound) return std::move(bound).error();
            f.bound = bound.value();
            return f;
        }
        if (diagonal) return parse_error("expected a comparison after the clock difference", i_ + 1);
        f.kind = Kind::Location;
        return f;
    }

    std::string_view s_;
    std::size_t i_{0};
};

bool binary(const StateFormula& f) { return f.kind == Kind::And || f.kind == Kind::Or || f.kind == Kind::Implies; }

std::string wrap(const StateFormula& f) { return binary(f) ? "(" + print_state(f) + ")" : print_state(f); }

void collect(const StateFormula& f, Atoms& out) {
    switch (f.kind) {
        case Kind::Location: out.locations.insert(f.name); break;
        case Kind::Clock:
            out.clocks.insert(f.name);
            if (f.minus) out.clocks.insert(*f.minus);
            break;
        case Kind::Semantic: out.semantic.push_back(f.formula); break;
        default: break;
    }
    for (const StateFormula& c : f.children) collect(c, out);
}

}  // namespace

std::string_view to_string(Quantifier q) noexcept {
    switch (q) {
        case Quantifier::Always: return "A[]";
        case Quantifier::Eventually: return "E<>";
        case Quantifier::Inevitably: return "A<>";
        case Quantifier::Potentially: return "E[]";
        case Quantifier::LeadsTo: return "-->";
    }
    return "A[]";
}

std::string_view to_string(Verdict v) noexcept {
    switch (v) {
        case Verdict::Satisfied: return "satisfied";
        case Verdict::Violated: return "violated";
        case Verdict::Inconclusive: return "inconclusive";
    }
    return "inconclusive";
}

Result<Property> parse_property(std::string_view text) { return Parser(text).run(); }

std::string print_state(const StateFormula& f) {
    switch (f.kind) {
        case Kind::True: return "true";
        case Kind::False: return "false";
        case Kind::Location: return f.name;
        case Kind::Clock:
            return f.name + (f.minus ? " - " + *f.minus : std::string()) + " " + std::string(ir::to_string(f.op)) + " " +
                   std::to_string(f.bound);
        case Kind::Semantic: return "sem(" + f.formula + ")";
        case Kind::Not: return "!" + (f.children.at(0).kind == Kind::Not || !binary(f.children.at(0))
                                          ? print_state(f.children.at(0))
                                          : "(" + print_state(f.children.at(0)) + ")");
        case Kind::And: return wrap(f.children.at(0)) + " && " + wrap(f.children.at(1));
        case Kind::Or: return wrap(f.children.at(0)) + " || " + wrap(f.children.at(1));
        case Kind::Implies: return wrap(f.children.at(0)) + " -> " + wrap(f.children.at(1));
    }
    return "true";
}

std::string print_property(const Property& p) {
    if (p.quantifier == Quantifier::LeadsTo) {
        return wrap(p.phi) + " --> " + (p.psi ? wrap(*p.psi) : std::string("true"));
    }
    return std::string(to_string(p.quantifier)) + " " + print_state(p.phi);
}

Atoms atoms(const StateFormula& formula) {
    Atoms out;
    collect(formula, out);
    return out;
}

Result<bool> holds(const StateFormula& f, const kernel::Model& model, const kernel::Configuration& c) {
    switch (f.kind) {
        case Kind::True: return true;
        case Kind::False: return false;
        case Kind::Location: {
            const std::optional<ir::LocationIndex> l = ir::find_location(model.ir(), f.name);
            if (!l) return make_error(ErrorCode::NotFound, "unknown location '" + f.name + "'");
            return c.location == *l;
        }
        case Kind::Clock: {
            const std::optional<ir::ClockIndex> x = ir::find_clock(model.ir(), f.name);
            if (!x) return make_error(ErrorCode::NotFound, "unknown clock '" + f.name + "'");
            Ticks value = kernel::clock_value(c, *x);
            if (f.minus) {
                const std::optional<ir::ClockIndex> y = ir::find_clock(model.ir(), *f.minus);
                if (!y) return make_error(ErrorCode::NotFound, "unknown clock '" + *f.minus + "'");
                value -= kernel::clock_value(c, *y);
            }
            Ticks bound = 0;
            if (__builtin_mul_overflow(f.bound, model.time_base().ticks_per_unit, &bound)) {
                return make_error(ErrorCode::ArithmeticOverflow, "bound too large");
            }
            switch (f.op) {
                case ir::Comparison::Less: return value < bound;
                case ir::Comparison::LessEqual: return value <= bound;
                case ir::Comparison::Equal: return value == bound;
                case ir::Comparison::GreaterEqual: return value >= bound;
                case ir::Comparison::Greater: return value > bound;
            }
            return false;
        }
        case Kind::Semantic:
            return make_error(ErrorCode::InvalidArgument,
                              "semantic atoms need observations and the ontology; Studio evaluates them");
        case Kind::Not: {
            Result<bool> a = holds(f.children.at(0), model, c);
            if (!a) return a;
            return !a.value();
        }
        case Kind::And:
        case Kind::Or:
        case Kind::Implies: {
            Result<bool> a = holds(f.children.at(0), model, c);
            if (!a) return a;
            Result<bool> b = holds(f.children.at(1), model, c);
            if (!b) return b;
            if (f.kind == Kind::And) return a.value() && b.value();
            if (f.kind == Kind::Or) return a.value() || b.value();
            return !a.value() || b.value();
        }
    }
    return false;
}

Result<Verdict> evaluate(const StateFormula& formula, const kernel::Model& model, const kernel::StateSet& states) {
    std::size_t satisfied = 0;
    for (const kernel::Configuration& c : states.members()) {
        Result<bool> h = holds(formula, model, c);
        if (!h) return std::move(h).error();
        if (h.value()) ++satisfied;
    }
    if (satisfied == states.members().size()) return Verdict::Satisfied;
    if (satisfied == 0) return Verdict::Violated;
    return Verdict::Inconclusive;
}

}  // namespace twin::monitoring
