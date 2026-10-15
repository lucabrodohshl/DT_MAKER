// Internal: the semantic compiler — OWL 2 + SHACL schema and RDF knowledge graph
// (04-compilation.md, section 4.3).
#pragma once

#include <string>

#include "resolve.hpp"
#include "twin/json/canonical.hpp"

namespace twin::model::detail {

struct RdfOutput {
    std::string schema_turtle;
    std::string graph_turtle;
    json::Json jsonld;
    json::Json trace = json::Json::array();
    std::size_t schema_triples{0};
    std::size_t graph_triples{0};
};

[[nodiscard]] RdfOutput compile_rdf(const Resolved& model);

/// The IRI base of a model ("urn:vts:<name>:" unless the model sets one).
[[nodiscard]] std::string iri_base(const Document& document);

/// QUDT unit local name for a unit symbol ("" if unknown).
[[nodiscard]] std::string qudt_unit(std::string_view unit);

}  // namespace twin::model::detail
