/**
 * @file xml_scan.cpp
 * @brief Internal: minimal XML reader (elements, attributes, text, comments, CDATA, line numbers).
 */
#include "xml_scan.hpp"

#include <cctype>
#include <charconv>
#include <optional>

namespace twin::authoring::detail {
namespace {

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string decode(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const std::size_t semi = s.find(';', i);
        if (semi == std::string_view::npos) {
            out += s[i];
            continue;
        }
        const std::string_view ent = s.substr(i + 1, semi - i - 1);
        if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "amp") out += '&';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            std::uint32_t cp = 0;
            const bool hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
            const std::string_view digits = ent.substr(hex ? 2 : 1);
            const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), cp, hex ? 16 : 10);
            if (ec != std::errc{} || ptr != digits.data() + digits.size()) {
                out += s.substr(i, semi - i + 1);
            } else {
                append_utf8(out, cp);
            }
        } else {
            out += s.substr(i, semi - i + 1);
        }
        i = semi;
    }
    return out;
}

class Reader {
public:
    explicit Reader(std::string_view s) : s_(s) {}

    Result<XmlNode> run() {
        XmlNode doc;
        doc.name = "#document";
        doc.line = 1;
        while (true) {
            skip_space();
            if (i_ >= s_.size()) break;
            if (starts("<?")) {
                if (!skip_until("?>")) return fail("unterminated processing instruction");
            } else if (starts("<!--")) {
                Result<XmlNode> c = comment();
                if (!c) return c;
                doc.children.push_back(std::move(c).value());
            } else if (starts("<!DOCTYPE")) {
                if (!skip_until(">")) return fail("unterminated DOCTYPE");
            } else if (starts("<")) {
                if (has_root_) return fail("more than one root element");
                Result<XmlNode> e = element();
                if (!e) return e;
                doc.children.push_back(std::move(e).value());
                has_root_ = true;
            } else {
                return fail("text outside the root element");
            }
        }
        if (!has_root_) return fail("no root element");
        return doc;
    }

private:
    [[nodiscard]] bool starts(std::string_view p) const { return s_.substr(i_, p.size()) == p; }

    void advance(std::size_t n = 1) {
        for (std::size_t k = 0; k < n && i_ < s_.size(); ++k) {
            if (s_[i_] == '\n') {
                ++line_;
                col_ = 1;
            } else {
                ++col_;
            }
            ++i_;
        }
    }

    void skip_space() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_])) != 0) advance();
    }

    bool skip_until(std::string_view end) {
        const std::size_t p = s_.find(end, i_);
        if (p == std::string_view::npos) return false;
        advance(p + end.size() - i_);
        return true;
    }

    Error fail(const std::string& what) const {
        return make_error(ErrorCode::ParseError, "XML is not well formed: " + what)
            .with("line", std::to_string(line_))
            .with("column", std::to_string(col_));
    }

    Result<XmlNode> comment() {
        XmlNode c;
        c.name = "#comment";
        c.line = line_;
        c.column = col_;
        advance(4);
        const std::size_t p = s_.find("-->", i_);
        if (p == std::string_view::npos) return fail("unterminated comment");
        c.text = std::string(s_.substr(i_, p - i_));
        advance(p + 3 - i_);
        return c;
    }

    std::string name() {
        const std::size_t b = i_;
        while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) != 0 || s_[i_] == '_' ||
                                  s_[i_] == '-' || s_[i_] == ':' || s_[i_] == '.')) {
            advance();
        }
        return std::string(s_.substr(b, i_ - b));
    }

    Result<XmlNode> element() {
        XmlNode e;
        e.line = line_;
        e.column = col_;
        advance();  // '<'
        e.name = name();
        if (e.name.empty()) return fail("missing element name");
        while (true) {
            skip_space();
            if (i_ >= s_.size()) return fail("unterminated tag <" + e.name + ">");
            if (starts("/>")) {
                advance(2);
                return e;
            }
            if (starts(">")) {
                advance();
                break;
            }
            const std::string key = name();
            if (key.empty()) return fail("bad attribute in <" + e.name + ">");
            skip_space();
            if (!starts("=")) return fail("attribute without value in <" + e.name + ">");
            advance();
            skip_space();
            if (i_ >= s_.size() || (s_[i_] != '"' && s_[i_] != '\'')) return fail("unquoted attribute value");
            const char quote = s_[i_];
            advance();
            const std::size_t end = s_.find(quote, i_);
            if (end == std::string_view::npos) return fail("unterminated attribute value");
            e.attributes[key] = decode(s_.substr(i_, end - i_));
            advance(end + 1 - i_);
        }
        // content
        while (true) {
            if (i_ >= s_.size()) return fail("missing </" + e.name + ">");
            if (starts("</")) {
                advance(2);
                const std::string closing = name();
                if (closing != e.name) return fail("</" + closing + "> closes <" + e.name + ">");
                skip_space();
                if (!starts(">")) return fail("bad closing tag");
                advance();
                return e;
            }
            if (starts("<!--")) {
                Result<XmlNode> c = comment();
                if (!c) return c;
                e.children.push_back(std::move(c).value());
            } else if (starts("<![CDATA[")) {
                const std::uint32_t tl = line_;
                advance(9);
                const std::size_t p = s_.find("]]>", i_);
                if (p == std::string_view::npos) return fail("unterminated CDATA");
                if (e.text_line == 0) e.text_line = tl;
                e.text += std::string(s_.substr(i_, p - i_));
                advance(p + 3 - i_);
            } else if (starts("<")) {
                Result<XmlNode> child = element();
                if (!child) return child;
                e.children.push_back(std::move(child).value());
            } else {
                const std::uint32_t tl = line_;
                const std::size_t p = s_.find('<', i_);
                const std::size_t end = p == std::string_view::npos ? s_.size() : p;
                if (e.text_line == 0) e.text_line = tl;
                e.text += decode(s_.substr(i_, end - i_));
                advance(end - i_);
            }
        }
    }

    std::string_view s_;
    std::size_t i_{0};
    std::uint32_t line_{1};
    std::uint32_t col_{1};
    bool has_root_{false};
};

}  // namespace

const XmlNode* XmlNode::child(std::string_view n) const {
    for (const XmlNode& c : children) {
        if (c.name == n) return &c;
    }
    return nullptr;
}

std::vector<const XmlNode*> XmlNode::children_named(std::string_view n) const {
    std::vector<const XmlNode*> out;
    for (const XmlNode& c : children) {
        if (c.name == n) out.push_back(&c);
    }
    return out;
}

std::optional<std::int64_t> XmlNode::int_attribute(std::string_view n) const {
    const auto it = attributes.find(std::string(n));
    if (it == attributes.end()) return std::nullopt;
    std::int64_t v = 0;
    const std::string& s = it->second;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
    return v;
}

Result<XmlNode> parse_xml(std::string_view xml) { return Reader(xml).run(); }

const XmlNode* resolve_path(const XmlNode& document, std::string_view path) {
    const XmlNode* node = &document;
    std::size_t i = 0;
    while (i < path.size()) {
        if (path[i] == '/') {
            ++i;
            continue;
        }
        const std::size_t slash = path.find('/', i);
        std::string_view step = path.substr(i, slash == std::string_view::npos ? std::string_view::npos : slash - i);
        i = slash == std::string_view::npos ? path.size() : slash;
        std::size_t index = 1;
        if (const std::size_t br = step.find('['); br != std::string_view::npos) {
            const std::string_view digits = step.substr(br + 1, step.find(']', br) - br - 1);
            std::from_chars(digits.data(), digits.data() + digits.size(), index);
            step = step.substr(0, br);
        }
        const std::vector<const XmlNode*> matches = node->children_named(step);
        if (index == 0 || index > matches.size()) return nullptr;
        node = matches[index - 1];
    }
    return node == &document ? nullptr : node;
}

}  // namespace twin::authoring::detail
