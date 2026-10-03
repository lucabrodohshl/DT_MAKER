/**
 * @file domain_parser.cpp
 * @brief Implementations of OntFileParser and InterpFileParser.
 *
 * Parses the custom .ont and .interp text file formats defined for the
 * CS1 Three-Tank benchmark, producing dtpta::Ontology and dtpta::InterpretationMap
 * objects that plug directly into the existing SemanticAlignmentChecker.
 */

#include "dtpta/domain_parser.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <regex>

namespace dtpta {

// ============================================================================
// Internal helpers (file-local)
// ============================================================================

namespace {

/** Trim leading/trailing whitespace. */
std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/** Strip a trailing inline comment (text after ';' outside parens). */
std::string strip_comment(const std::string& s) {
    int depth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '(') ++depth;
        else if (s[i] == ')') --depth;
        else if (s[i] == ';' && depth == 0) return trim(s.substr(0, i));
    }
    return trim(s);
}

/**
 * Convert C-style function application "f(n)" to LISP "(f n)".
 * Handles nested calls: "f(g(x),y)" → "(f (g x) y)".
 * Only converts patterns that look like identifiers followed by '('.
 */
std::string cstyle_to_smt2(const std::string& s) {
    // Use simple regex: word(... → (word ...
    // We iterate and do manual conversion to handle nesting correctly.
    std::string result;
    result.reserve(s.size() + 16);
    size_t i = 0;
    while (i < s.size()) {
        // Check for identifier followed by '('
        if (std::isalpha((unsigned char)s[i]) || s[i] == '_') {
            size_t j = i;
            while (j < s.size() && (std::isalnum((unsigned char)s[j]) || s[j] == '_')) ++j;
            if (j < s.size() && s[j] == '(') {
                // It's a C-style call: "ident("
                result += '(';
                result += s.substr(i, j - i); // function name
                result += ' ';
                // Skip the '('
                ++j;
                // Now convert args until matching ')'
                // Arguments are comma-separated; we recursively convert each arg
                int depth = 1;
                std::string arg_buf;
                while (j < s.size() && depth > 0) {
                    if (s[j] == '(') { ++depth; arg_buf += s[j]; }
                    else if (s[j] == ')') {
                        --depth;
                        if (depth == 0) {
                            // Flush last arg
                            std::string arg = trim(arg_buf);
                            if (!arg.empty()) result += cstyle_to_smt2(arg);
                        } else {
                            arg_buf += s[j];
                        }
                    } else if (s[j] == ',' && depth == 1) {
                        std::string arg = trim(arg_buf);
                        if (!arg.empty()) { result += cstyle_to_smt2(arg); result += ' '; }
                        arg_buf.clear();
                    } else {
                        arg_buf += s[j];
                    }
                    ++j;
                }
                result += ')';
                i = j;
                continue;
            } else {
                result += s.substr(i, j - i);
                i = j;
                continue;
            }
        }
        result += s[i];
        ++i;
    }
    return result;
}

/**
 * Build the SMT-LIB2 preamble string from an Ontology's declarations.
 * User-declared sorts are mapped to Real so numeric literals type-check.
 */
std::string build_preamble(const Ontology& ont) {
    std::ostringstream ss;
    ss << "(set-logic ALL)\n";

    // User-declared sorts → Real
    for (const auto& sd : ont.get_sorts()) {
        // Skip Z3 built-in sorts: they are already available, (declare-sort Real 0) fails
        if (sd.name == "Real" || sd.name == "real" ||
            sd.name == "Int"  || sd.name == "int"  ||
            sd.name == "Bool" || sd.name == "bool") continue;
        ss << "(declare-sort " << sd.name << " 0)\n";
    }

    for (const auto& fd : ont.get_functions()) {
        ss << "(declare-fun " << fd.name << " (";
        for (size_t k = 0; k < fd.arg_sorts.size(); ++k) {
            if (k > 0) ss << " ";
            ss << fd.arg_sorts[k];
        }
        ss << ") " << fd.return_sort << ")\n";
    }

    for (const auto& rd : ont.get_relations()) {
        ss << "(declare-fun " << rd.name << " (";
        for (size_t k = 0; k < rd.arg_sorts.size(); ++k) {
            if (k > 0) ss << " ";
            ss << rd.arg_sorts[k];
        }
        ss << ") Bool)\n";
    }

    return ss.str();
}

/**
 * Parse one SMT-LIB2 expression string, wrapping it in (assert ...).
 * Applies cstyle_to_smt2 preprocessing first.
 */
z3::expr parse_expr(z3::context& ctx,
                    const std::string& preamble,
                    const std::string& raw)
{
    std::string formula = trim(raw);

    if (formula == "true"  || formula == "True"  || formula == "TRUE")  return ctx.bool_val(true);
    if (formula == "false" || formula == "False" || formula == "FALSE") return ctx.bool_val(false);

    // Convert C-style calls
    std::string converted = cstyle_to_smt2(formula);

    std::string smt2 = preamble + "(assert " + converted + ")\n";
    try {
        z3::expr_vector assertions = ctx.parse_string(smt2.c_str());
        if (assertions.empty())
            throw std::runtime_error("OntFileParser: formula parsed to empty: " + raw);
        z3::expr result = assertions[0];
        for (unsigned k = 1; k < assertions.size(); ++k)
            result = result && assertions[k];
        return result;
    } catch (const z3::exception& e) {
        throw std::runtime_error(
            std::string("OntFileParser: Z3 parse error in formula '") +
            raw + "': " + e.msg());
    }
}

/**
 * Map a sort name to what Z3 should use.
 * Built-in sorts (Int, Real, Bool) pass through; everything else → Real.
 */
std::string map_sort(const std::string& name) {
    if (name == "Int" || name == "Real" || name == "Bool") return name;
    // User-declared domain sorts all live in Real arithmetic
    return "Real";
}

} // anonymous namespace

// ============================================================================
// OntFileParser::parse
// ============================================================================

std::shared_ptr<Ontology> OntFileParser::parse(const std::string& ont_path) {
    std::ifstream ifs(ont_path);
    if (!ifs.is_open())
        throw std::runtime_error("OntFileParser: cannot open '" + ont_path + "'");

    auto ont = std::make_shared<Ontology>();

    // --- First pass: collect all declarations (sorts, functions, relations) ---
    // We need two passes because axiom formulas reference the declared symbols.

    struct AxiomEntry { std::string id; std::string formula_str; };
    std::vector<AxiomEntry> axiom_entries;

    std::string line;
    while (std::getline(ifs, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;  // comment / blank

        // Strip inline comment
        line = strip_comment(line);
        if (line.empty()) continue;

        // Tokenise keyword
        std::istringstream ls(line);
        std::string kw;
        ls >> kw;

        if (kw == "sort") {
            std::string name;
            ls >> name;
            if (name.empty())
                throw std::runtime_error("OntFileParser: 'sort' missing name in: " + line);
            // Register the sort name; internally map to Real
            ont->add_sort(name);
        }
        else if (kw == "fun") {
            // fun name : [Arg ...] -> Ret
            // fun name : Ret             (nullary)
            std::string rest;
            std::getline(ls, rest);
            rest = trim(rest);

            // Split at ':'
            size_t colon = rest.find(':');
            if (colon == std::string::npos)
                throw std::runtime_error("OntFileParser: 'fun' missing ':' in: " + line);

            std::string fun_name = trim(rest.substr(0, colon));
            std::string sig      = trim(rest.substr(colon + 1));

            std::vector<std::string> arg_sorts;
            std::string ret_sort;

            size_t arrow = sig.find("->");
            if (arrow != std::string::npos) {
                // Has arguments
                std::string args_str = trim(sig.substr(0, arrow));
                ret_sort             = trim(sig.substr(arrow + 2));

                // Split arg_sorts by whitespace (each sort name is one token)
                std::istringstream as(args_str);
                std::string tok;
                while (as >> tok) arg_sorts.push_back(map_sort(tok));
            } else {
                // Nullary constant
                ret_sort = trim(sig);
            }
            ret_sort = map_sort(ret_sort);
            ont->add_function(fun_name, arg_sorts, ret_sort);
        }
        else if (kw == "rel") {
            // rel name : [Arg ...]    (predicate)
            // rel name :              (0-ary)
            std::string rest;
            std::getline(ls, rest);
            rest = trim(rest);

            size_t colon = rest.find(':');
            if (colon == std::string::npos)
                throw std::runtime_error("OntFileParser: 'rel' missing ':' in: " + line);

            std::string rel_name = trim(rest.substr(0, colon));
            std::string args_str = trim(rest.substr(colon + 1));

            std::vector<std::string> arg_sorts;
            std::istringstream as(args_str);
            std::string tok;
            while (as >> tok) arg_sorts.push_back(map_sort(tok));

            ont->add_relation(rel_name, arg_sorts);
        }
        else if (kw == "axiom") {
            // axiom id : formula
            std::string rest;
            std::getline(ls, rest);
            rest = trim(rest);

            size_t colon = rest.find(':');
            if (colon == std::string::npos)
                throw std::runtime_error("OntFileParser: 'axiom' missing ':' in: " + line);

            std::string ax_id  = trim(rest.substr(0, colon));
            std::string ax_fml = trim(rest.substr(colon + 1));
            axiom_entries.push_back({ax_id, ax_fml});
        }
        else {
            // Unknown keyword — skip (may be a blank or section header line)
        }
    }

    // --- Second pass: parse and add axioms using built preamble ---
    std::string preamble = build_preamble(*ont);
    z3::context& ctx = ont->get_context();

    for (const auto& ax : axiom_entries) {
        try {
            z3::expr expr = parse_expr(ctx, preamble, ax.formula_str);
            ont->add_axiom(expr);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("OntFileParser: error in axiom '") +
                ax.id + "': " + e.what());
        }
    }

    return ont;
}

// ============================================================================
// InterpFileParser::parse
// ============================================================================

InterpretationMap InterpFileParser::parse(const std::string& interp_path,
                                          std::shared_ptr<Ontology> ontology)
{
    std::ifstream ifs(interp_path);
    if (!ifs.is_open())
        throw std::runtime_error("InterpFileParser: cannot open '" + interp_path + "'");

    InterpretationMap imap;
    std::string preamble = build_preamble(*ontology);
    z3::context& ctx = ontology->get_context();

    std::string line;
    while (std::getline(ifs, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';') continue;

        // Strip inline comment — but be careful not to strip inside formulas.
        // Only strip if ';' appears outside balanced parentheses.
        line = strip_comment(line);
        if (line.empty()) continue;

        // Split at first ':'
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;  // malformed — skip

        std::string name    = trim(line.substr(0, colon));
        std::string formula = trim(line.substr(colon + 1));

        if (name.empty() || formula.empty()) continue;

        // Determine if this is an event label (ends with '!') or location name
        bool is_event = (!name.empty() && name.back() == '!');

        try {
            z3::expr expr = parse_expr(ctx, preamble, formula);
            if (is_event) {
                imap.set_event(name, expr);
            } else {
                imap.set_state(name, expr);
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("InterpFileParser: error parsing formula for '") +
                name + "': " + e.what());
        }
    }

    return imap;
}

} // namespace dtpta
