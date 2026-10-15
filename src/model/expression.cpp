#include "twin/model/expression.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace twin::model::expr {

namespace {

using Kind = Node::Kind;

bool alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool digit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

bool is_decimal(std::string_view text) noexcept {
    std::size_t i = 0;
    if (i < text.size() && text[i] == '-') ++i;
    const std::size_t start = i;
    while (i < text.size() && digit(text[i])) ++i;
    if (i == start) return false;
    if (i == text.size()) return true;
    if (text[i] != '.') return false;
    ++i;
    const std::size_t frac = i;
    while (i < text.size() && digit(text[i])) ++i;
    return i > frac && i == text.size();
}

bool Node::same(const Node& other) const {
    if (kind != other.kind || text != other.text || value != other.value || children.size() != other.children.size()) return false;
    for (std::size_t i = 0; i < children.size(); ++i) {
        if (!children[i].same(other.children[i])) return false;
    }
    return true;
}

// --------------------------------------------------------------------------- lexer + parser

namespace {

struct Token {
    enum class T { Ident, Number, Op, LParen, RParen, End, Bad };
    T type{T::End};
    std::string text;
    std::uint32_t column{1};
};

std::vector<Token> lex(std::string_view s) {
    std::vector<Token> out;
    std::size_t i = 0;
    const auto col = [](std::size_t at) { return static_cast<std::uint32_t>(at + 1); };
    while (i < s.size()) {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }
        const std::size_t start = i;
        if (alpha(c)) {
            while (i < s.size() && (alpha(s[i]) || digit(s[i]) || s[i] == '.')) ++i;
            out.push_back({Token::T::Ident, std::string(s.substr(start, i - start)), col(start)});
        } else if (digit(c)) {
            while (i < s.size() && digit(s[i])) ++i;
            if (i + 1 < s.size() && s[i] == '.' && digit(s[i + 1])) {
                ++i;
                while (i < s.size() && digit(s[i])) ++i;
            }
            out.push_back({Token::T::Number, std::string(s.substr(start, i - start)), col(start)});
        } else if (c == '(') {
            out.push_back({Token::T::LParen, "(", col(start)});
            ++i;
        } else if (c == ')') {
            out.push_back({Token::T::RParen, ")", col(start)});
            ++i;
        } else if ((c == '<' || c == '>' || c == '=' || c == '!') && i + 1 < s.size() && s[i + 1] == '=') {
            out.push_back({Token::T::Op, std::string(s.substr(start, 2)), col(start)});
            i += 2;
        } else if (c == '<' || c == '>' || c == '+' || c == '-' || c == '*') {
            out.push_back({Token::T::Op, std::string(1, c), col(start)});
            ++i;
        } else {
            out.push_back({Token::T::Bad, std::string(1, c), col(start)});
            ++i;
        }
    }
    out.push_back({Token::T::End, "", col(s.size())});
    return out;
}

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : t_(std::move(tokens)) {}

    Parsed run() {
        Parsed p;
        Node n = or_();
        if (failed_) {
            p.error = error_;
            p.column = column_;
            return p;
        }
        if (peek().type != Token::T::End) {
            p.error = "Unexpected '" + peek().text + "'.";
            p.column = peek().column;
            return p;
        }
        p.node = std::move(n);
        return p;
    }

private:
    const Token& peek() const { return t_[pos_]; }
    Token next() { return t_[pos_ < t_.size() - 1 ? pos_++ : pos_]; }
    bool is_word(std::string_view w) const { return peek().type == Token::T::Ident && peek().text == w; }
    bool is_op(std::string_view o) const { return peek().type == Token::T::Op && peek().text == o; }

    void fail(const std::string& message, std::uint32_t column) {
        if (!failed_) {
            failed_ = true;
            error_ = message;
            column_ = column;
        }
    }

    static Node binary(Kind k, Node a, Node b, std::uint32_t column, std::string text = {}) {
        Node n;
        n.kind = k;
        n.text = std::move(text);
        n.column = column;
        n.children.push_back(std::move(a));
        n.children.push_back(std::move(b));
        return n;
    }

    Node or_() {
        Node left = and_();
        while (!failed_ && is_word("or")) {
            const auto c = next().column;
            left = binary(Kind::Or, std::move(left), and_(), c);
        }
        return left;
    }

    Node and_() {
        Node left = not_();
        while (!failed_ && is_word("and")) {
            const auto c = next().column;
            left = binary(Kind::And, std::move(left), not_(), c);
        }
        return left;
    }

    Node not_() {
        if (is_word("not")) {
            const auto c = next().column;
            Node n;
            n.kind = Kind::Not;
            n.column = c;
            n.children.push_back(not_());
            return n;
        }
        return cmp();
    }

    Node cmp() {
        Node left = sum();
        for (const char* op : {"<=", ">=", "==", "!=", "<", ">"}) {
            if (!failed_ && is_op(op)) {
                const auto c = next().column;
                return binary(Kind::Compare, std::move(left), sum(), c, op);
            }
        }
        return left;
    }

    Node sum() {
        Node left = product();
        while (!failed_ && (is_op("+") || is_op("-"))) {
            const Token op = next();
            left = binary(op.text == "+" ? Kind::Add : Kind::Sub, std::move(left), product(), op.column);
        }
        return left;
    }

    Node product() {
        Node left = unary();
        while (!failed_ && is_op("*")) {
            const auto c = next().column;
            left = binary(Kind::Mul, std::move(left), unary(), c);
        }
        return left;
    }

    Node unary() {
        if (is_op("-")) {
            const auto c = next().column;
            Node n;
            n.kind = Kind::Neg;
            n.column = c;
            n.children.push_back(unary());
            return n;
        }
        return atom();
    }

    Node atom() {
        const Token tok = next();
        Node n;
        n.column = tok.column;
        switch (tok.type) {
            case Token::T::Number:
                n.kind = Kind::Number;
                n.text = tok.text;
                return n;
            case Token::T::Ident:
                if (tok.text == "true" || tok.text == "false") {
                    n.kind = Kind::Boolean;
                    n.value = tok.text == "true";
                    return n;
                }
                if (tok.text == "and" || tok.text == "or" || tok.text == "not") {
                    fail("Expected a value before '" + tok.text + "'.", tok.column);
                    return n;
                }
                if (tok.text.back() == '.' || tok.text.find("..") != std::string::npos) {
                    fail("Malformed path '" + tok.text + "'.", tok.column);
                    return n;
                }
                n.kind = Kind::Path;
                n.text = tok.text;
                return n;
            case Token::T::LParen: {
                Node inner = or_();
                if (!failed_ && peek().type != Token::T::RParen) {
                    fail("Expected ')'.", peek().column);
                    return inner;
                }
                next();
                return inner;
            }
            case Token::T::End:
                fail("Unexpected end of expression.", tok.column);
                return n;
            case Token::T::Bad:
                fail("Unexpected character '" + tok.text + "'.", tok.column);
                return n;
            default:
                fail("Unexpected '" + tok.text + "'.", tok.column);
                return n;
        }
    }

    std::vector<Token> t_;
    std::size_t pos_{0};
    bool failed_{false};
    std::string error_;
    std::uint32_t column_{0};
};

std::string trim(std::string_view s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])) != 0) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])) != 0) --b;
    return std::string(s.substr(a, b - a));
}

}  // namespace

Parsed parse(std::string_view text) {
    const std::string t = trim(text);
    if (t.rfind("smt:", 0) == 0) {
        Parsed p;
        Node n;
        n.kind = Kind::Raw;
        n.text = trim(std::string_view(t).substr(4));
        if (n.text.empty()) {
            p.error = "Empty SMT-LIB2 formula.";
            p.column = 5;
            return p;
        }
        p.node = std::move(n);
        return p;
    }
    if (t.empty()) {
        Parsed p;
        p.error = "Empty expression.";
        p.column = 1;
        return p;
    }
    return Parser(lex(text)).run();
}

// --------------------------------------------------------------------------- printing

namespace {

int precedence(const Node& n) {
    switch (n.kind) {
        case Kind::Or: return 1;
        case Kind::And: return 2;
        case Kind::Not: return 3;
        case Kind::Compare: return 4;
        case Kind::Add:
        case Kind::Sub: return 5;
        case Kind::Mul: return 6;
        case Kind::Neg: return 7;
        default: return 8;
    }
}

std::string print_at(const Node& n, int min_prec);

std::string infix(const Node& n, const char* op, int prec) {
    // Left operand may have the same precedence (left-associative chains); the right one must bind tighter.
    return print_at(n.children[0], prec) + op + print_at(n.children[1], prec + 1);
}

std::string print_at(const Node& n, int min_prec) {
    std::string s;
    switch (n.kind) {
        case Kind::Number: s = n.text; break;
        case Kind::Boolean: s = n.value ? "true" : "false"; break;
        case Kind::Path: s = n.text; break;
        case Kind::Raw: s = "smt:" + n.text; break;
        case Kind::Or: s = infix(n, " or ", 1); break;
        case Kind::And: s = infix(n, " and ", 2); break;
        case Kind::Not: s = "not " + print_at(n.children[0], 3); break;
        case Kind::Compare:
            s = print_at(n.children[0], 5) + " " + n.text + " " + print_at(n.children[1], 5);
            break;
        case Kind::Add: s = infix(n, " + ", 5); break;
        case Kind::Sub: s = infix(n, " - ", 5); break;
        case Kind::Mul: s = infix(n, " * ", 6); break;
        case Kind::Neg: s = "-" + print_at(n.children[0], 7); break;
    }
    return precedence(n) < min_prec ? "(" + s + ")" : s;
}

}  // namespace

std::string print(const Node& node) { return print_at(node, 0); }

// --------------------------------------------------------------------------- checking + translation

namespace {

struct Val {
    std::string smt;
    Symbol::Type type{Symbol::Type::Number};
    std::vector<std::string> literals;  // enum-valued term
    bool constant{false};               // numeric constant (linearity)
    bool unresolved_literal{false};     // a bare identifier that may be an enum literal
    std::string name;                   // the identifier of an unresolved literal
};

class Translator {
public:
    explicit Translator(const Resolver& resolve) : resolve_(resolve) {}

    std::optional<CheckError> error;
    std::vector<std::string> symbols;

    Val run(const Node& n) {
        Val v = term(n);
        if (error) return v;
        if (v.unresolved_literal) {
            fail("TML022", "Unknown name '" + v.name + "'.", n.column);
        } else if (v.type != Symbol::Type::Boolean) {
            fail("TML021", "A condition must be true or false; this expression is a number.", n.column);
        }
        return v;
    }

private:
    void fail(const char* code, const std::string& message, std::uint32_t column) {
        if (!error) error = CheckError{code, message, column};
    }

    void use(const std::string& symbol) {
        if (std::find(symbols.begin(), symbols.end(), symbol) == symbols.end()) symbols.push_back(symbol);
    }

    /// Flatten a chain of `kind` into operands (left-nested and right-nested alike for associative ops).
    static void chain(const Node& n, Kind kind, bool associative, std::vector<const Node*>& out) {
        if (n.kind == kind) {
            chain(n.children[0], kind, associative, out);
            if (associative) {
                chain(n.children[1], kind, associative, out);
            } else {
                out.push_back(&n.children[1]);
            }
        } else {
            out.push_back(&n);
        }
    }

    Val boolean_op(const Node& n, const char* op) {
        std::vector<const Node*> parts;
        chain(n, n.kind, true, parts);
        Val out;
        out.type = Symbol::Type::Boolean;
        out.smt = std::string("(") + op;
        for (const Node* p : parts) {
            Val v = expect(*p, Symbol::Type::Boolean);
            out.smt += " " + v.smt;
        }
        out.smt += ")";
        return out;
    }

    Val arith_op(const Node& n, const char* op, bool associative) {
        std::vector<const Node*> parts;
        chain(n, n.kind, associative, parts);
        Val out;
        out.type = Symbol::Type::Number;
        out.constant = true;
        out.smt = std::string("(") + op;
        for (const Node* p : parts) {
            Val v = expect(*p, Symbol::Type::Number);
            out.constant = out.constant && v.constant;
            out.smt += " " + v.smt;
        }
        out.smt += ")";
        return out;
    }

    Val expect(const Node& n, Symbol::Type type) {
        Val v = term(n);
        if (error) return v;
        if (v.unresolved_literal) {
            fail("TML022", "Unknown name '" + v.name + "'.", n.column);
            return v;
        }
        if (v.type == Symbol::Type::String) {
            fail("TML011", "Text properties cannot be used in conditions.", n.column);
            return v;
        }
        const bool numeric = v.type == Symbol::Type::Number;
        if (type == Symbol::Type::Number && !numeric) {
            fail("TML021", v.type == Symbol::Type::Enum ? "An enumeration can only be compared with == or != to one of its values."
                                                        : "Expected a number here, found a condition.",
                 n.column);
        } else if (type == Symbol::Type::Boolean && v.type != Symbol::Type::Boolean) {
            fail("TML021", "Expected a condition here, found a number.", n.column);
        }
        return v;
    }

    Val term(const Node& n) {
        Val out;
        if (error) return out;
        switch (n.kind) {
            case Kind::Number:
                if (!is_decimal(n.text)) fail("TML020", "Malformed number '" + n.text + "'.", n.column);
                out.smt = n.text;
                out.constant = true;
                return out;
            case Kind::Boolean:
                out.type = Symbol::Type::Boolean;
                out.smt = n.value ? "true" : "false";
                return out;
            case Kind::Raw:
                out.type = Symbol::Type::Boolean;
                out.smt = normalize_smt(n.text);
                return out;
            case Kind::Path: {
                const auto sym = resolve_(n.text);
                if (!sym) {
                    out.unresolved_literal = true;
                    out.name = n.text;
                    return out;
                }
                out.type = sym->type;
                out.smt = sym->smt;
                out.literals = sym->literals;
                if (sym->type != Symbol::Type::String) use(sym->smt);
                return out;
            }
            case Kind::Not: {
                Val v = expect(n.children[0], Symbol::Type::Boolean);
                out.type = Symbol::Type::Boolean;
                out.smt = "(not " + v.smt + ")";
                return out;
            }
            case Kind::And: return boolean_op(n, "and");
            case Kind::Or: return boolean_op(n, "or");
            case Kind::Add: return arith_op(n, "+", true);
            case Kind::Sub: return arith_op(n, "-", false);
            case Kind::Mul: {
                Val r = arith_op(n, "*", true);
                // Linear arithmetic: at most one operand of the product may be non-constant.
                std::vector<const Node*> parts;
                chain(n, Kind::Mul, true, parts);
                int variables = 0;
                for (const Node* p : parts) {
                    Translator probe(resolve_);
                    const Val v = probe.term(*p);
                    if (!v.constant) ++variables;
                }
                if (variables > 1) fail("TML023", "Products of two measured or fixed values are not linear; multiply by a number.", n.column);
                return r;
            }
            case Kind::Neg: {
                Val v = expect(n.children[0], Symbol::Type::Number);
                out.smt = "(- " + v.smt + ")";
                out.constant = v.constant;
                return out;
            }
            case Kind::Compare: return compare(n);
        }
        return out;
    }

    Val compare(const Node& n) {
        Val out;
        out.type = Symbol::Type::Boolean;
        const std::string& op = n.text;
        Val a = term(n.children[0]);
        Val b = term(n.children[1]);
        if (error) return out;
        // Enumeration comparisons: one side an enum term, the other one of its literals.
        const auto literal_code = [](const Val& e, const Val& lit) -> std::optional<std::string> {
            const std::string name = lit.unresolved_literal ? lit.name : std::string{};
            if (name.empty()) return std::nullopt;
            for (std::size_t i = 0; i < e.literals.size(); ++i) {
                if (e.literals[i] == name) return std::to_string(i);
            }
            return std::nullopt;
        };
        if (a.type == Symbol::Type::Enum || b.type == Symbol::Type::Enum) {
            const Val& e = a.type == Symbol::Type::Enum ? a : b;
            const Val& lit = a.type == Symbol::Type::Enum ? b : a;
            if (op != "==" && op != "!=") {
                fail("TML021", "An enumeration can only be compared with == or !=.", n.column);
                return out;
            }
            std::string rhs;
            if (const auto code = literal_code(e, lit)) {
                rhs = *code;
            } else if (lit.type == Symbol::Type::Enum && !lit.unresolved_literal && lit.literals == e.literals) {
                rhs = lit.smt;
            } else {
                fail("TML021", "Compare an enumeration with one of its values (" + join(e.literals) + ").", n.children[1].column);
                return out;
            }
            out.smt = op == "==" ? "(= " + e.smt + " " + rhs + ")" : "(not (= " + e.smt + " " + rhs + "))";
            return out;
        }
        for (const Val* v : {&a, &b}) {
            if (v->unresolved_literal) {
                fail("TML022", "Unknown name '" + v->name + "'.", n.column);
                return out;
            }
            if (v->type == Symbol::Type::String) {
                fail("TML011", "Text properties cannot be used in conditions.", n.column);
                return out;
            }
        }
        if (op == "==" || op == "!=") {
            if (a.type != b.type) {
                fail("TML021", "Both sides of " + op + " must be numbers or both conditions.", n.column);
                return out;
            }
            out.smt = op == "==" ? "(= " + a.smt + " " + b.smt + ")" : "(not (= " + a.smt + " " + b.smt + "))";
            return out;
        }
        if (a.type != Symbol::Type::Number || b.type != Symbol::Type::Number) {
            fail("TML021", "Only numbers can be compared with " + op + ".", n.column);
            return out;
        }
        out.smt = "(" + op + " " + a.smt + " " + b.smt + ")";
        return out;
    }

    static std::string join(const std::vector<std::string>& v) {
        std::string s;
        for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
        return s;
    }

    const Resolver& resolve_;
};

}  // namespace

Translated to_smt(const Node& node, const Resolver& resolve) {
    Translator t(resolve);
    const Val v = t.run(node);
    Translated out;
    if (t.error) {
        out.error = t.error;
        return out;
    }
    out.smt = v.smt;
    out.symbols = t.symbols;
    return out;
}

Translated condition(std::string_view text, const Resolver& resolve) {
    const Parsed p = parse(text);
    if (!p.node) {
        Translated out;
        out.error = CheckError{"TML020", p.error, p.column};
        return out;
    }
    return to_smt(*p.node, resolve);
}

// --------------------------------------------------------------------------- SMT-LIB2 reading (lifting)

namespace {

struct SExpr {
    bool list{false};
    std::string atom;
    std::vector<SExpr> items;
};

class SReader {
public:
    explicit SReader(std::string_view s) : s_(s) {}

    std::optional<SExpr> read_all() {
        auto e = read();
        skip();
        if (!e || pos_ != s_.size()) return std::nullopt;
        return e;
    }

private:
    void skip() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_])) != 0) ++pos_;
    }

    std::optional<SExpr> read() {
        skip();
        if (pos_ >= s_.size()) return std::nullopt;
        if (s_[pos_] == '(') {
            ++pos_;
            SExpr list;
            list.list = true;
            for (;;) {
                skip();
                if (pos_ >= s_.size()) return std::nullopt;
                if (s_[pos_] == ')') {
                    ++pos_;
                    return list;
                }
                auto item = read();
                if (!item) return std::nullopt;
                list.items.push_back(std::move(*item));
            }
        }
        if (s_[pos_] == ')' || s_[pos_] == '|' || s_[pos_] == '"') return std::nullopt;
        const std::size_t start = pos_;
        while (pos_ < s_.size() && s_[pos_] != '(' && s_[pos_] != ')' && std::isspace(static_cast<unsigned char>(s_[pos_])) == 0) ++pos_;
        SExpr a;
        a.atom = std::string(s_.substr(start, pos_ - start));
        return a;
    }

    std::string_view s_;
    std::size_t pos_{0};
};

bool symbol_like(const std::string& a) {
    if (a.empty() || !alpha(a.front())) return false;
    return std::all_of(a.begin(), a.end(), [](char c) { return alpha(c) || digit(c); });
}

std::optional<Node> lift(const SExpr& e);

std::optional<Node> chain_of(Kind kind, const std::vector<SExpr>& args, std::size_t from, bool left_assoc) {
    if (args.size() < from + 2) return std::nullopt;
    auto acc = lift(args[from]);
    if (!acc) return std::nullopt;
    for (std::size_t i = from + 1; i < args.size(); ++i) {
        auto rhs = lift(args[i]);
        if (!rhs) return std::nullopt;
        Node n;
        n.kind = kind;
        n.children.push_back(std::move(*acc));
        n.children.push_back(std::move(*rhs));
        acc = std::move(n);
    }
    (void)left_assoc;
    return acc;
}

std::optional<Node> lift(const SExpr& e) {
    if (!e.list) {
        Node n;
        if (e.atom == "true" || e.atom == "false") {
            n.kind = Kind::Boolean;
            n.value = e.atom == "true";
            return n;
        }
        if (is_decimal(e.atom) && e.atom.front() != '-') {
            n.kind = Kind::Number;
            n.text = e.atom;
            return n;
        }
        if (symbol_like(e.atom) && e.atom != "and" && e.atom != "or" && e.atom != "not") {
            n.kind = Kind::Path;
            n.text = e.atom;
            return n;
        }
        return std::nullopt;
    }
    if (e.items.empty() || e.items.front().list) return std::nullopt;
    const std::string& head = e.items.front().atom;
    const std::size_t argc = e.items.size() - 1;
    if (head == "and" || head == "or") return chain_of(head == "and" ? Kind::And : Kind::Or, e.items, 1, true);
    if (head == "+") return chain_of(Kind::Add, e.items, 1, true);
    if (head == "*") return chain_of(Kind::Mul, e.items, 1, true);
    if (head == "-") {
        if (argc == 1) {
            auto x = lift(e.items[1]);
            if (!x) return std::nullopt;
            Node n;
            n.kind = Kind::Neg;
            n.children.push_back(std::move(*x));
            return n;
        }
        return chain_of(Kind::Sub, e.items, 1, true);
    }
    if (head == "not" && argc == 1) {
        const SExpr& inner = e.items[1];
        if (inner.list && inner.items.size() == 3 && !inner.items.front().list && inner.items.front().atom == "=") {
            auto a = lift(inner.items[1]);
            auto b = lift(inner.items[2]);
            if (!a || !b) return std::nullopt;
            Node n;
            n.kind = Kind::Compare;
            n.text = "!=";
            n.children.push_back(std::move(*a));
            n.children.push_back(std::move(*b));
            return n;
        }
        auto x = lift(inner);
        if (!x) return std::nullopt;
        Node n;
        n.kind = Kind::Not;
        n.children.push_back(std::move(*x));
        return n;
    }
    if ((head == "=" || head == "<" || head == "<=" || head == ">" || head == ">=") && argc == 2) {
        auto a = lift(e.items[1]);
        auto b = lift(e.items[2]);
        if (!a || !b) return std::nullopt;
        Node n;
        n.kind = Kind::Compare;
        n.text = head == "=" ? "==" : head;
        n.children.push_back(std::move(*a));
        n.children.push_back(std::move(*b));
        return n;
    }
    return std::nullopt;
}

}  // namespace

std::optional<Node> from_smt(std::string_view smt) {
    auto e = SReader(smt).read_all();
    if (!e) return std::nullopt;
    return lift(*e);
}

std::vector<std::string> conjuncts(std::string_view smt) {
    const std::string s = normalize_smt(smt);
    if (s.rfind("(and ", 0) != 0 || s.back() != ')') return {s};
    std::vector<std::string> out;
    int depth = 0;
    std::size_t start = 5;
    for (std::size_t i = 5; i + 1 < s.size(); ++i) {
        const char c = s[i];
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (depth < 0) return {s};  // the leading "(and" closes before the end: not one conjunction
        if (c == ' ' && depth == 0) {
            if (i > start) out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    if (depth != 0) return {s};
    if (s.size() - 1 > start) out.push_back(s.substr(start, s.size() - 1 - start));
    return out;
}

std::string normalize_smt(std::string_view smt) {
    std::string out;
    bool space = false;
    for (char c : smt) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            space = true;
            continue;
        }
        if (space && !out.empty() && out.back() != '(' && c != ')') out += ' ';
        space = false;
        out += c;
    }
    return out;
}

}  // namespace twin::model::expr
