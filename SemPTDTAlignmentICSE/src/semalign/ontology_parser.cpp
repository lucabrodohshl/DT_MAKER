/**
 * @file ontology_parser.cpp
 * @brief Parse a JSON ontology + interpretation specification into DomainKnowledge.
 *
 * JSON format (all sections optional):
 * {
 *   "sorts":    ["SortA", ...],
 *   "functions": [{"name":"f","args":["Int"],"return":"Int"}, ...],
 *   "relations": [{"name":"R","args":["Int"]}, ...],
 *   "axioms":    ["(smtlib2 expr)", ...],
 *   "label_interpretations": {
 *       "PT": {"label_a": "(smtlib2 expr)", ...},
 *       "DT": {"label_b": "(smtlib2 expr)", ...}
 *   },
 *   "state_interpretations": {
 *       "PT": {"LocationName": "(smtlib2 expr)", ...},
 *       "DT": {"LocationName": "(smtlib2 expr)", ...}
 *   }
 * }
 *
 * All formula strings use SMT-LIB2 expression syntax. They are parsed via
 * z3::context::parse_string() using a helper (assert ...) wrapper, with the
 * declared sorts and function/relation symbols pre-loaded as context.
 */

#include "dtpta/ontology_parser.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>

// nlohmann/json single-header (included via CMake FetchContent)
#include <nlohmann/json.hpp>

namespace dtpta {

// -------------------------------------------------------------------------- //
//  Internal helpers
// -------------------------------------------------------------------------- //

namespace {

/**
 * Build the SMT-LIB2 preamble (sort + function declarations) from an Ontology.
 * This preamble is prepended to formula strings before parsing.
 */
std::string build_smt2_preamble(const Ontology& ont) {
    std::ostringstream ss;
    ss << "(set-logic ALL)\n";

    for (const auto& sd : ont.get_sorts()) {
        // Skip Z3 built-in sorts: (declare-sort Real 0) etc. will fail
        if (sd.name == "Real" || sd.name == "real" ||
            sd.name == "Int"  || sd.name == "int"  ||
            sd.name == "Bool" || sd.name == "bool") continue;
        ss << "(declare-sort " << sd.name << " 0)\n";
    }

    for (const auto& fd : ont.get_functions()) {
        ss << "(declare-fun " << fd.name << " (";
        for (size_t i = 0; i < fd.arg_sorts.size(); ++i) {
            if (i > 0) ss << " ";
            ss << fd.arg_sorts[i];
        }
        ss << ") " << fd.return_sort << ")\n";
    }

    for (const auto& rd : ont.get_relations()) {
        ss << "(declare-fun " << rd.name << " (";
        for (size_t i = 0; i < rd.arg_sorts.size(); ++i) {
            if (i > 0) ss << " ";
            ss << rd.arg_sorts[i];
        }
        ss << ") Bool)\n";
    }

    return ss.str();
}

/**
 * Parse a single SMT-LIB2 expression string as a Boolean Z3 expr.
 * Wraps the expression in "(assert ...)" so that parse_string returns it.
 */
z3::expr parse_formula(z3::context& ctx,
                        const std::string& preamble,
                        const std::string& formula_str)
{
    // Trim whitespace
    std::string trimmed = formula_str;
    while (!trimmed.empty() && std::isspace((unsigned char)trimmed.front())) trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace((unsigned char)trimmed.back()))  trimmed.pop_back();

    // Shortcuts for trivial formulas
    if (trimmed == "true"  || trimmed == "True"  || trimmed == "TRUE")
        return ctx.bool_val(true);
    if (trimmed == "false" || trimmed == "False" || trimmed == "FALSE")
        return ctx.bool_val(false);

    std::string smt2 = preamble + "(assert " + trimmed + ")\n";
    try {
        z3::expr_vector assertions = ctx.parse_string(smt2.c_str());
        if (assertions.empty()) {
            throw std::runtime_error("OntologyParser: formula parsed to empty: " + formula_str);
        }
        // parse_string may return multiple assertions; conjoin them
        if (assertions.size() == 1) return assertions[0];
        z3::expr result = assertions[0];
        for (unsigned i = 1; i < assertions.size(); ++i) {
            result = result && assertions[i];
        }
        return result;
    } catch (const z3::exception& e) {
        throw std::runtime_error(
            std::string("OntologyParser: Z3 parse error for formula '") +
            formula_str + "': " + e.msg());
    }
}

} // anonymous namespace

// -------------------------------------------------------------------------- //
//  OntologyParser::parse
// -------------------------------------------------------------------------- //

DomainKnowledge OntologyParser::parse(const std::string& json_path) {
    // Try to open the file
    std::ifstream ifs(json_path);
    if (!ifs.is_open()) {
        std::cerr << "[OntologyParser] Warning: cannot open '" << json_path
                  << "', returning trivial DomainKnowledge.\n";
        return make_trivial();
    }

    nlohmann::json jroot;
    try {
        ifs >> jroot;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(
            std::string("OntologyParser: JSON parse error in '") +
            json_path + "': " + e.what());
    }

    // ----- Build the Ontology -----
    auto ont = std::make_shared<Ontology>();
    z3::context& ctx = ont->get_context();

    // 1. Register sorts
    if (jroot.contains("sorts")) {
        for (const auto& s : jroot["sorts"]) {
            ont->add_sort(s.get<std::string>());
        }
    }

    // 2. Register functions
    if (jroot.contains("functions")) {
        for (const auto& f : jroot["functions"]) {
            std::string name = f.at("name").get<std::string>();
            std::string ret  = f.value("return", "Int");
            std::vector<std::string> args;
            if (f.contains("args")) {
                for (const auto& a : f["args"]) {
                    args.push_back(a.get<std::string>());
                }
            }
            ont->add_function(name, args, ret);
        }
    }

    // 3. Register relations
    if (jroot.contains("relations")) {
        for (const auto& r : jroot["relations"]) {
            std::string name = r.at("name").get<std::string>();
            std::vector<std::string> args;
            if (r.contains("args")) {
                for (const auto& a : r["args"]) {
                    args.push_back(a.get<std::string>());
                }
            }
            ont->add_relation(name, args);
        }
    }

    // Build preamble once all declarations are registered
    std::string preamble = build_smt2_preamble(*ont);

    // 4. Parse and add axioms
    if (jroot.contains("axioms")) {
        for (const auto& ax_str : jroot["axioms"]) {
            z3::expr ax = parse_formula(ctx, preamble, ax_str.get<std::string>());
            ont->add_axiom(ax);
        }
    }

    // ----- Build DomainKnowledge -----
    DomainKnowledge dk(std::move(ont));

    // Helper lambda to fill an InterpretationMap from a JSON object
    auto fill_label_interp = [&](InterpretationMap& imap,
                                 const nlohmann::json& jobj)
    {
        for (auto it = jobj.begin(); it != jobj.end(); ++it) {
            std::string label   = it.key();
            std::string formula = it.value().get<std::string>();
            z3::expr e = parse_formula(ctx, preamble, formula);
            imap.set_event(label, e);
        }
    };

    auto fill_state_interp = [&](InterpretationMap& imap,
                                  const nlohmann::json& jobj)
    {
        for (auto it = jobj.begin(); it != jobj.end(); ++it) {
            std::string loc_name = it.key();
            std::string formula  = it.value().get<std::string>();
            z3::expr e = parse_formula(ctx, preamble, formula);
            imap.set_state(loc_name, e);
        }
    };

    // 5. Label interpretations
    if (jroot.contains("label_interpretations")) {
        const auto& linterp = jroot["label_interpretations"];
        if (linterp.contains("PT")) fill_label_interp(dk.pt_interp, linterp["PT"]);
        if (linterp.contains("DT")) fill_label_interp(dk.dt_interp, linterp["DT"]);
    }

    // 6. State interpretations (optional)
    if (jroot.contains("state_interpretations")) {
        const auto& sinterp = jroot["state_interpretations"];
        if (sinterp.contains("PT")) fill_state_interp(dk.pt_interp, sinterp["PT"]);
        if (sinterp.contains("DT")) fill_state_interp(dk.dt_interp, sinterp["DT"]);
    }

    return dk;
}

// -------------------------------------------------------------------------- //
//  OntologyParser::make_trivial
// -------------------------------------------------------------------------- //

DomainKnowledge OntologyParser::make_trivial() {
    return DomainKnowledge(std::make_shared<Ontology>());
}

} // namespace dtpta
