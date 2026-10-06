/**
 * @file text_lexer.cpp
 * @brief Internal: TwinTA lexer (comments are tokens, so the parser can attach them as notes).
 */
#include "text_lexer.hpp"

#include <array>
#include <cctype>

namespace twin::authoring::detail {
namespace {

std::string trim(std::string_view s) {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(" \t\r");
    return std::string(s.substr(b, e - b + 1));
}

/// `// text` -> "text" (all leading slashes and one space removed).
std::string line_comment_text(std::string_view body) {
    std::size_t i = 0;
    while (i < body.size() && body[i] == '/') ++i;
    if (i < body.size() && body[i] == ' ') ++i;
    return trim(body.substr(i));
}

/// Block comment content: lines trimmed, javadoc-style leading '*' removed, empty edge lines dropped.
std::string block_comment_text(std::string_view inner) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= inner.size()) {
        const std::size_t nl = inner.find('\n', start);
        std::string line = trim(inner.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start));
        if (!line.empty() && line.front() == '*') {
            line.erase(0, 1);
            if (!line.empty() && line.front() == ' ') line.erase(0, 1);
        }
        lines.push_back(trim(line));
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    while (!lines.empty() && lines.front().empty()) lines.erase(lines.begin());
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    std::string out;
    for (std::size_t k = 0; k < lines.size(); ++k) {
        if (k > 0) out += '\n';
        out += lines[k];
    }
    return out;
}

class Lexer {
public:
    explicit Lexer(std::string_view s) : s_(s) {}

    LexResult run() {
        while (true) {
            skip_blanks();
            if (i_ >= s_.size()) break;
            const SourceRange start = here();
            const char c = s_[i_];
            if (c == '/' && peek(1) == '/') {
                const std::size_t b = i_;
                while (i_ < s_.size() && s_[i_] != '\n') advance();
                push(Tok::Comment, line_comment_text(s_.substr(b, i_ - b)), start);
            } else if (c == '/' && peek(1) == '*') {
                block_comment(start);
            } else if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
                const std::size_t b = i_;
                while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) != 0 || s_[i_] == '_')) advance();
                push(Tok::Ident, std::string(s_.substr(b, i_ - b)), start);
            } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                const std::size_t b = i_;
                while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_])) != 0) advance();
                push(Tok::Int, std::string(s_.substr(b, i_ - b)), start);
            } else if (c == '"') {
                string_literal(start);
            } else if (!punct(start)) {
                const std::string bad = character_at(i_);
                advance();
                out_.diagnostics.push_back(Diagnostic{"error", "TWT005", "unexpected character '" + bad + "'", {},
                                                      {"document", "", ""}, close(start)});
            }
        }
        Token end{Tok::End, "", here()};
        end.range.end_line = end.range.line;
        end.range.end_column = end.range.column;
        out_.tokens.push_back(end);
        return std::move(out_);
    }

private:
    [[nodiscard]] char peek(std::size_t k) const { return i_ + k < s_.size() ? s_[i_ + k] : '\0'; }

    [[nodiscard]] SourceRange here() const { return SourceRange{line_, col_, line_, col_}; }

    /// The range from @p start to the current position.
    [[nodiscard]] SourceRange close(SourceRange start) const {
        start.end_line = line_;
        start.end_column = col_;
        return start;
    }

    void advance() {
        const auto c = static_cast<unsigned char>(s_[i_]);
        ++i_;
        if (c == '\n') {
            ++line_;
            col_ = 1;
        } else if ((c & 0xC0U) != 0x80U) {  // count code points, not UTF-8 continuation bytes
            ++col_;
        }
        // Continuation bytes belong to the code point already counted.
        while (i_ < s_.size() && (static_cast<unsigned char>(s_[i_]) & 0xC0U) == 0x80U) ++i_;
    }

    [[nodiscard]] std::string character_at(std::size_t i) const {
        std::size_t n = 1;
        while (i + n < s_.size() && (static_cast<unsigned char>(s_[i + n]) & 0xC0U) == 0x80U) ++n;
        return std::string(s_.substr(i, n));
    }

    void skip_blanks() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\r' || s_[i_] == '\n')) advance();
    }

    void push(Tok kind, std::string text, SourceRange start) {
        out_.tokens.push_back(Token{kind, std::move(text), close(start)});
    }

    void block_comment(SourceRange start) {
        advance();
        advance();
        const std::size_t b = i_;
        while (i_ < s_.size() && !(s_[i_] == '*' && peek(1) == '/')) advance();
        if (i_ >= s_.size()) {
            out_.diagnostics.push_back(Diagnostic{"error", "TWT002", "unterminated block comment", "close it with */",
                                                  {"document", "", ""}, close(start)});
            return;
        }
        const std::string inner(s_.substr(b, i_ - b));
        advance();
        advance();
        push(Tok::Comment, block_comment_text(inner), start);
    }

    void string_literal(SourceRange start) {
        advance();
        std::string text;
        while (i_ < s_.size() && s_[i_] != '"' && s_[i_] != '\n') {
            if (s_[i_] == '\\' && (peek(1) == '"' || peek(1) == '\\')) advance();
            text += character_at(i_);
            advance();
        }
        if (i_ >= s_.size() || s_[i_] != '"') {
            out_.diagnostics.push_back(Diagnostic{"error", "TWT004", "unterminated string", "close it with \"",
                                                  {"document", "", ""}, close(start)});
            return;
        }
        advance();
        push(Tok::String, std::move(text), start);
    }

    bool punct(SourceRange start) {
        static constexpr std::array<std::string_view, 8> kTwo = {"->", "&&", "||", "<=", ">=", "==", ":=", "!="};
        for (std::string_view p : kTwo) {
            if (s_.substr(i_, 2) == p) {
                advance();
                advance();
                push(Tok::Punct, std::string(p), start);
                return true;
            }
        }
        static constexpr std::string_view kOne = "{};,:-!?<>=()";
        if (kOne.find(s_[i_]) != std::string_view::npos) {
            const std::string p(1, s_[i_]);
            advance();
            push(Tok::Punct, p, start);
            return true;
        }
        return false;
    }

    std::string_view s_;
    std::size_t i_{0};
    std::uint32_t line_{1};
    std::uint32_t col_{1};
    LexResult out_;
};

}  // namespace

LexResult lex(std::string_view source) { return Lexer(source).run(); }

}  // namespace twin::authoring::detail
