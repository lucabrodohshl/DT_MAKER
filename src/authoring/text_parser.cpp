/**
 * @file text_parser.cpp
 * @brief TwinTA parser: recursive descent with error recovery, notes from comments, symbols and ranges.
 */
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <tuple>

#include "text_lexer.hpp"
#include "twin/authoring/text.hpp"
#include "twin/authoring/validate.hpp"

namespace twin::authoring {
namespace {

using detail::Tok;
using detail::Token;

const std::set<std::string, std::less<>>& keywords() {
    static const std::set<std::string, std::less<>> k = {"automaton", "clock",     "const", "channel", "initial",
                                                         "location",  "invariant", "edge",  "guard",   "sync",
                                                         "reset",     "true",      "false"};
    return k;
}

const std::set<std::string, std::less<>>& declaration_keywords() {
    static const std::set<std::string, std::less<>> k = {"clock", "const", "channel", "initial", "location", "edge"};
    return k;
}

/// UPPAAL constructs outside the fragment, with the reason the toolchain refuses them.
const std::map<std::string, std::string, std::less<>>& outside_fragment() {
    static const std::map<std::string, std::string, std::less<>> k = {
        {"int", "data variables are not supported: the aligner gives them no semantics (TWC014)"},
        {"bool", "data variables are not supported: the aligner gives them no semantics (TWC014)"},
        {"double", "data variables are not supported: the aligner gives them no semantics (TWC014)"},
        {"scalar", "data variables are not supported: the aligner gives them no semantics (TWC014)"},
        {"meta", "data variables are not supported: the aligner gives them no semantics (TWC014)"},
        {"typedef", "type declarations are not supported (only clocks, integer constants and channels)"},
        {"struct", "type declarations are not supported (only clocks, integer constants and channels)"},
        {"void", "functions are not supported: the aligner does not evaluate function bodies (TWC008)"},
        {"urgent", "urgent locations and channels are not supported: the aligner ignores urgency (TWC031/TWC012)"},
        {"committed", "committed locations are not supported: the aligner ignores urgency (TWC031)"},
        {"broadcast", "broadcast channels are not supported: the aligner ignores channel kinds (TWC012)"},
        {"chan", "UPPAAL 'chan' declarations are written 'channel' in TwinTA"},
        {"process", "process and system declarations are generated, not written (one automaton per view)"},
        {"system", "process and system declarations are generated, not written (one automaton per view)"}};
    return k;
}

/// Where a note goes: the automaton, or the declaration of @p kind at @p index.
struct NoteRef {
    enum class Kind { Model, Clock, Constant, Channel, Location, Edge } kind{Kind::Model};
    std::size_t index{0};
};

/// A use of a name, resolved against the declarations after parsing.
struct Reference {
    std::string kind;
    std::string name;
    SourceRange range;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : t_(std::move(tokens)) {}

    // ------------------------------------------------------------------ text
    ParseResult parse_document(std::vector<Diagnostic> lexical) {
        syntax_ = std::move(lexical);
        parse_automaton();
        ParseResult r;
        if (!syntax_.empty()) {
            std::sort(syntax_.begin(), syntax_.end(), [](const Diagnostic& a, const Diagnostic& b) {
                return std::tie(a.range->line, a.range->column) < std::tie(b.range->line, b.range->column);
            });
            r.diagnostics = std::move(syntax_);
            r.symbols = symbols();
            return r;
        }
        assign_edge_ids();
        r.diagnostics = locate(validate(m_));
        r.symbols = symbols();
        r.model = std::move(m_);
        return r;
    }

    // ------------------------------------------------------------ constraint
    Result<Constraint> parse_standalone_constraint() {
        if (peek().kind == Tok::End) return Constraint{};
        Constraint c;
        const bool ok = constraint(c);
        if (ok && at_punct("||")) {
            syntax("TWT010", peek().range, "disjunction '||' is not supported",
                   "the aligner reads '||' as '&&'; split the edge into one edge per disjunct");
        } else if (ok && peek().kind != Tok::End) {
            unexpected("the end of the constraint");
        }
        if (!syntax_.empty()) {
            const Diagnostic& d = syntax_.front();
            return make_error(ErrorCode::ParseError, d.message + (d.hint.empty() ? "" : " (" + d.hint + ")"))
                .with("code", d.code)
                .with("column", std::to_string(d.range ? d.range->column : 0));
        }
        return c;
    }

private:
    // ------------------------------------------------------------ token access
    const Token& peek() {
        while (t_[pos_].kind == Tok::Comment) {
            comment(t_[pos_]);
            ++pos_;
        }
        return t_[pos_];
    }
    const Token& next() {
        const Token& tok = peek();
        if (tok.kind != Tok::End) ++pos_;
        last_line_ = tok.range.end_line;
        return tok;
    }
    bool at_punct(std::string_view p) { return peek().kind == Tok::Punct && peek().text == p; }
    bool at_word(std::string_view w) { return peek().kind == Tok::Ident && peek().text == w; }
    bool accept_punct(std::string_view p) {
        if (!at_punct(p)) return false;
        next();
        return true;
    }

    static std::string describe(const Token& tok) {
        switch (tok.kind) {
            case Tok::End: return "the end of the text";
            case Tok::String: return "\"" + tok.text + "\"";
            default: return "'" + tok.text + "'";
        }
    }

    void syntax(std::string code, const SourceRange& range, std::string message, std::string hint = {}) {
        syntax_.push_back(
            Diagnostic{"error", std::move(code), std::move(message), std::move(hint), {"document", "", ""}, range});
    }
    void unexpected(const std::string& expected, std::string hint = {}) {
        const Token& tok = peek();
        syntax("TWT001", tok.range, "expected " + expected + ", found " + describe(tok), std::move(hint));
    }

    /// Skip to the end of the current declaration: a ';' (consumed), a '}' closing
    /// the enclosing block, or the start of the next declaration.
    void recover_declaration() {
        int depth = 0;
        while (peek().kind != Tok::End) {
            const Token& tok = peek();
            if (tok.kind == Tok::Punct && tok.text == "{") ++depth;
            if (tok.kind == Tok::Punct && tok.text == "}") {
                if (depth == 0) return;
                --depth;
                next();
                if (depth == 0) return;
                continue;
            }
            if (depth == 0 && tok.kind == Tok::Punct && tok.text == ";") {
                next();
                return;
            }
            if (depth == 0 && tok.kind == Tok::Ident && declaration_keywords().contains(tok.text)) return;
            next();
        }
    }

    /// Skip to the end of a block item: a ';' (consumed) or the block's '}'.
    void recover_item() {
        while (peek().kind != Tok::End && !at_punct("}")) {
            if (at_punct(";")) {
                next();
                return;
            }
            next();
        }
    }

    std::optional<Token> name(const std::string& what) {
        const Token& tok = peek();
        if (tok.kind != Tok::Ident || keywords().contains(tok.text)) {
            unexpected("a " + what + " name");
            return std::nullopt;
        }
        return next();
    }

    // ----------------------------------------------------------------- notes
    std::string* note(const NoteRef& r) {
        switch (r.kind) {
            case NoteRef::Kind::Model: return &m_.note;
            case NoteRef::Kind::Clock: return &m_.clocks[r.index].note;
            case NoteRef::Kind::Constant: return &m_.constants[r.index].note;
            case NoteRef::Kind::Channel: return &m_.channels[r.index].note;
            case NoteRef::Kind::Location: return &m_.locations[r.index].note;
            case NoteRef::Kind::Edge: return &m_.edges[r.index].note;
        }
        return &m_.note;
    }
    static void append(std::string& note, const std::string& text) {
        if (!note.empty()) note += '\n';
        note += text;
    }
    void comment(const Token& c) {
        if (current_) {
            append(*note(*current_), c.text);
        } else if (previous_ && c.range.line == previous_line_) {
            append(*note(*previous_), c.text);
        } else if (in_automaton_) {
            pending_.push_back(c.text);
        } else {
            append(m_.note, c.text);
        }
    }
    /// A declaration element now exists: it receives the pending comments and the following ones.
    void begin(NoteRef r) {
        for (const std::string& p : pending_) append(*note(r), p);
        pending_.clear();
        current_ = r;
    }
    void end() {
        if (current_) {
            previous_ = current_;
            previous_line_ = last_line_;
        }
        current_.reset();
    }

    // ------------------------------------------------------------ structure
    void parse_automaton() {
        if (!at_word("automaton")) {
            unexpected("'automaton'");
            return;
        }
        next();
        std::optional<Token> n = name("automaton");
        if (!n) return;
        m_.name = n->text;
        define("model", n->text, n->range);
        if (!accept_punct("{")) {
            unexpected("'{'");
            return;
        }
        in_automaton_ = true;
        while (!at_punct("}") && peek().kind != Tok::End) declaration();
        in_automaton_ = false;
        for (const std::string& p : pending_) append(m_.note, p);
        pending_.clear();
        if (!accept_punct("}")) {
            unexpected("'}' closing the automaton");
            return;
        }
        previous_.reset();
        if (peek().kind != Tok::End) unexpected("the end of the text (one automaton per document)");
    }

    void declaration() {
        const Token& tok = peek();
        if (tok.kind == Tok::Ident) {
            if (tok.text == "clock" || tok.text == "channel") {
                names_declaration(tok.text);
                return;
            }
            if (tok.text == "const") {
                constant_declaration();
                return;
            }
            if (tok.text == "initial" || tok.text == "location") {
                location_declaration();
                return;
            }
            if (tok.text == "edge") {
                edge_declaration();
                return;
            }
            if (const auto it = outside_fragment().find(tok.text); it != outside_fragment().end()) {
                syntax("TWT014", tok.range, "'" + tok.text + "' is outside the supported fragment", it->second);
                next();
                recover_declaration();
                return;
            }
        }
        unexpected("a declaration (clock, const, channel, location or edge)");
        if (peek().kind != Tok::End && !at_punct("}")) next();
        recover_declaration();
    }

    void names_declaration(const std::string& keyword) {
        next();
        const bool clock = keyword == "clock";
        bool first = true;
        while (true) {
            std::optional<Token> n = name(keyword);
            if (!n) {
                recover_declaration();
                return;
            }
            define(keyword, n->text, n->range);
            if (clock) {
                m_.clocks.push_back(ClockDecl{n->text, ""});
                if (first) begin({NoteRef::Kind::Clock, m_.clocks.size() - 1});
            } else {
                m_.channels.push_back(ChannelDecl{n->text, ""});
                if (first) begin({NoteRef::Kind::Channel, m_.channels.size() - 1});
            }
            first = false;
            if (!accept_punct(",")) break;
        }
        if (!accept_punct(";")) {
            unexpected("',' or ';' in the " + keyword + " declaration");
            end();
            recover_declaration();
            return;
        }
        end();
    }

    std::optional<std::int64_t> integer(bool negative) {
        const Token& tok = peek();
        if (tok.kind != Tok::Int) {
            unexpected("an integer");
            return std::nullopt;
        }
        const Token lit = next();
        try {
            std::size_t used = 0;
            const long long v = std::stoll(lit.text, &used);
            return negative ? -static_cast<std::int64_t>(v) : static_cast<std::int64_t>(v);
        } catch (const std::exception&) {
            syntax("TWT003", lit.range, "integer " + lit.text + " is out of range", "use a value below 2^63");
            return std::nullopt;
        }
    }

    void constant_declaration() {
        next();
        if (at_word("int")) next();
        std::optional<Token> n = name("constant");
        if (!n) {
            recover_declaration();
            return;
        }
        define("constant", n->text, n->range);
        m_.constants.push_back(ConstantDecl{n->text, 0, ""});
        begin({NoteRef::Kind::Constant, m_.constants.size() - 1});
        if (!accept_punct("=")) {
            unexpected("'=' and the constant's value");
            end();
            recover_declaration();
            return;
        }
        const bool negative = accept_punct("-");
        std::optional<std::int64_t> v = integer(negative);
        if (!v) {
            end();
            recover_declaration();
            return;
        }
        m_.constants.back().value = *v;
        if (!accept_punct(";")) {
            unexpected("';' after the constant");
            end();
            recover_declaration();
            return;
        }
        end();
    }

    void location_declaration() {
        bool initial = false;
        if (at_word("initial")) {
            next();
            initial = true;
            if (!at_word("location")) {
                unexpected("'location' after 'initial'");
                recover_declaration();
                return;
            }
        }
        next();
        std::optional<Token> n = name("location");
        if (!n) {
            recover_declaration();
            return;
        }
        define("location", n->text, n->range);
        m_.locations.push_back(LocationDecl{n->text, initial, {}, ""});
        const std::size_t index = m_.locations.size() - 1;
        begin({NoteRef::Kind::Location, index});
        if (accept_punct(";")) {
            end();
            return;
        }
        if (!accept_punct("{")) {
            unexpected("';' or '{' after the location name");
            end();
            recover_declaration();
            return;
        }
        while (!at_punct("}") && peek().kind != Tok::End) {
            if (!at_word("invariant")) {
                unexpected("'invariant' or '}'");
                recover_item();
                continue;
            }
            const Token kw = next();
            Constraint c;
            if (!constraint(c) || !expect_item_end()) {
                recover_item();
                continue;
            }
            part("location", n->text, "invariant", kw.range);
            auto& inv = m_.locations[index].invariant;
            inv.insert(inv.end(), c.begin(), c.end());
        }
        if (!accept_punct("}")) unexpected("'}' closing the location");
        end();
    }

    bool expect_item_end() {
        if (accept_punct(";")) return true;
        if (at_punct("||")) {
            syntax("TWT010", peek().range, "disjunction '||' is not supported",
                   "the aligner reads '||' as '&&'; split the edge into one edge per disjunct");
        } else {
            unexpected("'&&' or ';'");
        }
        return false;
    }

    void edge_declaration() {
        const Token kw = next();
        std::string id;
        std::optional<Token> first;
        if (peek().kind == Tok::String) {
            first = next();
            if (!at_punct(":")) {
                unexpected("':' after the edge id");
                recover_declaration();
                return;
            }
        } else {
            first = name("location");
            if (!first) {
                recover_declaration();
                return;
            }
        }
        std::optional<Token> source;
        if (accept_punct(":")) {
            id = first->text;
            source = name("source location");
            if (!source) {
                recover_declaration();
                return;
            }
        } else {
            source = first;
        }
        m_.edges.push_back(EdgeDecl{id, source->text, "", std::nullopt, {}, {}, ""});
        const std::size_t index = m_.edges.size() - 1;
        edge_ranges_[index]["name"] = id.empty() ? kw.range : first->range;
        edge_ranges_[index]["source"] = source->range;
        refer("location", source->text, source->range);
        begin({NoteRef::Kind::Edge, index});
        if (!accept_punct("->")) {
            unexpected("'->' between source and target");
            end();
            recover_declaration();
            return;
        }
        std::optional<Token> target = name("target location");
        if (!target) {
            end();
            recover_declaration();
            return;
        }
        m_.edges[index].target = target->text;
        edge_ranges_[index]["target"] = target->range;
        refer("location", target->text, target->range);
        if (accept_punct(";")) {
            end();
            return;
        }
        if (!accept_punct("{")) {
            unexpected("';' or '{' after the target location");
            end();
            recover_declaration();
            return;
        }
        while (!at_punct("}") && peek().kind != Tok::End) edge_item(index);
        if (!accept_punct("}")) unexpected("'}' closing the edge");
        end();
    }

    void edge_item(std::size_t index) {
        EdgeDecl& e = m_.edges[index];
        if (at_word("guard")) {
            const Token kw = next();
            if (seen_guard_.contains(index)) {
                syntax("TWT001", kw.range, "an edge has one guard", "combine the constraints with '&&'");
                recover_item();
                return;
            }
            seen_guard_.insert(index);
            Constraint c;
            if (!constraint(c) || !expect_item_end()) {
                recover_item();
                return;
            }
            m_.edges[index].guard = std::move(c);
            edge_ranges_[index]["guard"] = kw.range;
            return;
        }
        if (at_word("sync")) {
            const Token kw = next();
            if (e.sync) {
                syntax("TWT001", kw.range, "an edge has at most one synchronisation");
                recover_item();
                return;
            }
            std::optional<Token> ch = name("channel");
            if (!ch) {
                recover_item();
                return;
            }
            char direction = '!';
            if (accept_punct("?")) {
                direction = '?';
            } else if (!accept_punct("!")) {
                unexpected("'!' or '?' after the channel");
                recover_item();
                return;
            }
            if (!accept_punct(";")) {
                unexpected("';' after the synchronisation");
                recover_item();
                return;
            }
            m_.edges[index].sync = Sync{ch->text, direction};
            edge_ranges_[index]["sync"] = kw.range;
            refer("channel", ch->text, ch->range);
            return;
        }
        if (at_word("reset")) {
            const Token kw = next();
            if (!edge_ranges_[index].contains("reset")) edge_ranges_[index]["reset"] = kw.range;
            while (true) {
                std::optional<Token> clock = name("clock");
                if (!clock) {
                    recover_item();
                    return;
                }
                if (accept_punct(":=") || accept_punct("=")) {
                    std::optional<std::int64_t> v = integer(accept_punct("-"));
                    if (!v) {
                        recover_item();
                        return;
                    }
                    if (*v != 0) {
                        SourceRange r = clock->range;
                        r.end_line = last_line_;
                        r.end_column = t_[pos_ - 1].range.end_column;
                        syntax("TWT013", r, "clock '" + clock->text + "' can only be reset to 0",
                               "the aligner ignores non-zero clock resets");
                        recover_item();
                        return;
                    }
                }
                m_.edges[index].resets.push_back(clock->text);
                refer("clock", clock->text, clock->range);
                if (!accept_punct(",")) break;
            }
            if (!accept_punct(";")) {
                unexpected("',' or ';' in the reset list");
                recover_item();
                return;
            }
            return;
        }
        unexpected("'guard', 'sync', 'reset' or '}'");
        recover_item();
    }

    /// constraint = "true" | atom { "&&" atom }.
    bool constraint(Constraint& out) {
        if (at_word("true")) {
            next();
            return true;
        }
        if (at_word("false")) {
            syntax("TWT012", peek().range, "the constant 'false' is not supported",
                   "the aligner reads an unparseable constraint as true; remove the edge or location instead");
            return false;
        }
        while (true) {
            if (!atom(out)) return false;
            if (!accept_punct("&&")) return true;
        }
    }

    bool atom(Constraint& out) {
        if (at_punct("!")) {
            syntax("TWT011", peek().range, "negation is not supported", "the aligner ignores '!'; state the constraint positively");
            return false;
        }
        std::optional<Token> clock = name("clock");
        if (!clock) return false;
        Atom a;
        a.clock = clock->text;
        refer("clock", clock->text, clock->range);
        if (accept_punct("-")) {
            std::optional<Token> minus = name("clock");
            if (!minus) return false;
            a.minus = minus->text;
            refer("clock", minus->text, minus->range);
        }
        const Token& op = peek();
        if (op.kind == Tok::Punct && op.text == "=") {
            unexpected("a comparison", "use '==' for equality");
            return false;
        }
        if (op.kind == Tok::Punct && op.text == "!=") {
            syntax("TWT011", op.range, "'!=' is a negation and is not supported", "state the constraint positively");
            return false;
        }
        const std::optional<ir::Comparison> cmp =
            op.kind == Tok::Punct ? ir::parse_comparison(op.text) : std::nullopt;
        if (!cmp) {
            unexpected("a comparison (<, <=, ==, >=, >)");
            return false;
        }
        next();
        a.op = *cmp;
        if (peek().kind == Tok::Ident && !keywords().contains(peek().text)) {
            const Token c = next();
            a.bound = Bound{c.text};
            refer("constant", c.text, c.range);
        } else {
            const bool negative = accept_punct("-");
            if (peek().kind != Tok::Int) {
                unexpected("an integer or a constant name");
                return false;
            }
            std::optional<std::int64_t> v = integer(negative);
            if (!v) return false;
            a.bound = Bound{*v};
        }
        out.push_back(std::move(a));
        return true;
    }

    // ---------------------------------------------------------- symbols etc.
    void define(const std::string& kind, const std::string& name, const SourceRange& range) {
        definitions_.push_back({kind, name, range});
    }
    void refer(const std::string& kind, const std::string& name, const SourceRange& range) {
        references_.push_back({kind, name, range});
    }
    void part(const std::string& kind, const std::string& name, const std::string& part, const SourceRange& range) {
        part_ranges_.emplace(std::tuple{kind, name, part}, range);
    }

    void assign_edge_ids() {
        std::set<std::string> used;
        for (const EdgeDecl& e : m_.edges) {
            if (!e.id.empty()) used.insert(e.id);
        }
        std::size_t n = 1;
        for (EdgeDecl& e : m_.edges) {
            if (!e.id.empty()) continue;
            while (used.contains("e" + std::to_string(n))) ++n;
            e.id = "e" + std::to_string(n);
            used.insert(e.id);
        }
        for (std::size_t i = 0; i < m_.edges.size(); ++i) {
            for (const auto& [p, range] : edge_ranges_[i]) {
                part_ranges_.emplace(std::tuple{std::string("edge"), m_.edges[i].id, p}, range);
            }
        }
    }

    std::vector<Symbol> symbols() const {
        std::vector<Symbol> out;
        for (const Reference& d : definitions_) out.push_back(Symbol{d.kind, d.name, d.range, {}});
        for (std::size_t i = 0; i < m_.edges.size(); ++i) {
            if (m_.edges[i].id.empty()) continue;
            const auto it = edge_ranges_.find(i);
            if (it != edge_ranges_.end() && it->second.contains("name")) {
                out.push_back(Symbol{"edge", m_.edges[i].id, it->second.at("name"), {}});
            }
        }
        for (const Reference& r : references_) {
            for (Symbol& s : out) {
                if (s.kind == r.kind && s.name == r.name) {
                    s.references.push_back(r.range);
                    break;
                }
            }
        }
        return out;
    }

    /// Give each structural diagnostic the range of its element (or of the element's part).
    std::vector<Diagnostic> locate(std::vector<Diagnostic> ds) const {
        for (Diagnostic& d : ds) {
            const auto p = part_ranges_.find(std::tuple{d.element.kind, d.element.name, d.element.part});
            if (p != part_ranges_.end()) {
                d.range = p->second;
                continue;
            }
            if (d.element.kind == "edge") {
                const auto e = part_ranges_.find(std::tuple{std::string("edge"), d.element.name, std::string("name")});
                if (e != part_ranges_.end()) d.range = e->second;
                continue;
            }
            for (const Reference& def : definitions_) {
                if (def.kind == d.element.kind && def.name == d.element.name) {
                    d.range = def.range;
                    break;
                }
            }
        }
        return ds;
    }

    std::vector<Token> t_;
    std::size_t pos_{0};
    std::uint32_t last_line_{0};
    Model m_;
    std::vector<Diagnostic> syntax_;
    // notes
    bool in_automaton_{false};
    std::vector<std::string> pending_;
    std::optional<NoteRef> current_;
    std::optional<NoteRef> previous_;
    std::uint32_t previous_line_{0};
    // symbols and ranges
    std::vector<Reference> definitions_;
    std::vector<Reference> references_;
    std::map<std::size_t, std::map<std::string, SourceRange>> edge_ranges_;
    std::map<std::tuple<std::string, std::string, std::string>, SourceRange> part_ranges_;
    std::set<std::size_t> seen_guard_;
};

}  // namespace

ParseResult parse_text(std::string_view source) {
    detail::LexResult lexed = detail::lex(source);
    return Parser(std::move(lexed.tokens)).parse_document(std::move(lexed.diagnostics));
}

Result<std::string> format_text(std::string_view source) {
    ParseResult r = parse_text(source);
    if (!r.model) {
        const Diagnostic& d = r.diagnostics.front();
        return make_error(ErrorCode::ParseError, "the text has syntax errors: " + d.message)
            .with("code", d.code)
            .with("line", std::to_string(d.range ? d.range->line : 0));
    }
    return print_text(*r.model);
}

Result<Constraint> parse_constraint(std::string_view text) {
    detail::LexResult lexed = detail::lex(text);
    if (!lexed.diagnostics.empty()) {
        return make_error(ErrorCode::ParseError, lexed.diagnostics.front().message).with("code", lexed.diagnostics.front().code);
    }
    return Parser(std::move(lexed.tokens)).parse_standalone_constraint();
}

json::Json to_json(const std::vector<Symbol>& symbols) {
    json::Json out = json::Json::array();
    for (const Symbol& s : symbols) {
        json::Json refs = json::Json::array();
        for (const SourceRange& r : s.references) refs.push_back(to_json(r));
        out.push_back({{"kind", s.kind}, {"name", s.name}, {"definition", to_json(s.definition)}, {"references", refs}});
    }
    return out;
}

}  // namespace twin::authoring
