#include "twin/model/compile.hpp"

#include "behavior.hpp"
#include "contract.hpp"
#include "rdf.hpp"
#include "resolve.hpp"
#include "theory.hpp"
#include "twin/authoring/uppaal.hpp"
#include "validate.hpp"

namespace twin::model {

Compiled compile(const Document& document) {
    Compiled out;
    out.semantic_digest = semantic_digest(document);
    std::vector<Diagnostic>& diags = out.diagnostics;
    const detail::Resolved resolved(document, diags);
    detail::validate(document, resolved, diags);
    if (has_errors(diags) || !resolved.ok()) return out;

    const detail::BehaviorOutput behavior = detail::compile_behavior(resolved, diags);
    if (has_errors(diags)) return out;
    if (!behavior.automaton) {
        diags.push_back({"warning", "TML050", "No asset has a behaviour, so no timed automaton is generated.",
                         "Add states and transitions to the system or one of its assets.", "model", ""});
    }
    detail::TheoryOutput theory = detail::compile_theory(resolved, behavior, diags);
    if (has_errors(diags)) return out;
    detail::RdfOutput rdf = detail::compile_rdf(resolved);

    Artifacts a;
    a.automaton = behavior.automaton;
    if (a.automaton) a.automaton_xml = authoring::render_toolchain_xml(*a.automaton);
    a.ontology = std::move(theory.ontology);
    a.dt_interpretation = std::move(theory.dt_interpretation);
    if (document.reference) {
        a.reference = document.reference->automaton;
        a.reference_xml = authoring::render_toolchain_xml(document.reference->automaton);
        a.pt_interpretation = std::move(theory.pt_interpretation);
    }
    a.schema_turtle = std::move(rdf.schema_turtle);
    a.graph_turtle = std::move(rdf.graph_turtle);
    a.graph_jsonld = std::move(rdf.jsonld);
    a.sections = detail::derive_sections(resolved, behavior);

    json::Json trace = json::Json::array();
    for (const json::Json* part : std::initializer_list<const json::Json*>{&behavior.trace, &theory.trace, &rdf.trace}) {
        for (const json::Json& e : *part) trace.push_back(e);
    }
    a.trace = std::move(trace);

    std::size_t assets = resolved.instances().size();
    a.summary = {{"compiler", std::string(kCompilerVersion)},
                 {"digest", out.semantic_digest},
                 {"assets", assets},
                 {"machines", behavior.machines},
                 {"locations", a.automaton ? a.automaton->locations.size() : 0},
                 {"edges", a.automaton ? a.automaton->edges.size() : 0},
                 {"clocks", a.automaton ? a.automaton->clocks.size() : 0},
                 {"channels", a.automaton ? a.automaton->channels.size() : 0},
                 {"symbols", theory.symbols},
                 {"axioms", theory.axioms},
                 {"schemaTriples", rdf.schema_triples},
                 {"graphTriples", rdf.graph_triples}};
    out.artifacts = std::move(a);
    return out;
}

Compiled compile(const json::Json& document) {
    Decoded d = decode(document);
    if (has_errors(d.diagnostics)) {
        Compiled out;
        out.diagnostics = std::move(d.diagnostics);
        return out;
    }
    Compiled out = compile(d.document);
    out.diagnostics.insert(out.diagnostics.begin(), d.diagnostics.begin(), d.diagnostics.end());
    return out;
}

std::vector<Diagnostic> check(const json::Json& document) {
    Decoded d = decode(document);
    if (has_errors(d.diagnostics)) return d.diagnostics;
    std::vector<Diagnostic> diags = d.diagnostics;
    const detail::Resolved resolved(d.document, diags);
    detail::validate(d.document, resolved, diags);
    return diags;
}

json::Json to_json(const Artifacts& a) {
    json::Json out = {{"automaton", a.automaton ? authoring::to_json(*a.automaton) : json::Json(nullptr)},
                      {"automatonXml", a.automaton_xml},
                      {"ontology", a.ontology},
                      {"dtInterpretation", a.dt_interpretation},
                      {"schemaTurtle", a.schema_turtle},
                      {"graphTurtle", a.graph_turtle},
                      {"jsonld", a.graph_jsonld},
                      {"sections", a.sections},
                      {"trace", a.trace},
                      {"summary", a.summary}};
    if (a.reference) {
        out["reference"] = authoring::to_json(*a.reference);
        out["referenceXml"] = a.reference_xml;
        out["ptInterpretation"] = a.pt_interpretation;
    }
    return out;
}

}  // namespace twin::model
