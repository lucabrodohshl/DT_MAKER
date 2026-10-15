// The semantic compiler: OWL 2 + SHACL schema (T-box) and the RDF knowledge graph
// (A-box) of a model, written as sorted Turtle without blank nodes and as JSON-LD
// (04-compilation.md, section 4.3).
#include "rdf.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <tuple>

#include "behavior.hpp"
#include "theory.hpp"
#include "twin/model/compile.hpp"

namespace twin::model::detail {

std::string iri_base(const Document& d) { return d.iri_base.empty() ? "urn:vts:" + d.name + ":" : d.iri_base; }

std::string qudt_unit(std::string_view unit) {
    struct Row {
        std::string_view unit;
        std::string_view qudt;
    };
    static constexpr std::array<Row, 34> kUnits{{
        {"degC", "DEG_C"}, {"degF", "DEG_F"}, {"K", "K"}, {"bar", "BAR"}, {"mbar", "MilliBAR"}, {"Pa", "PA"}, {"kPa", "KiloPA"},
        {"MPa", "MegaPA"}, {"psi", "PSI"}, {"%", "PERCENT"}, {"ratio", "UNITLESS"}, {"A", "A"}, {"mA", "MilliA"}, {"V", "V"},
        {"kV", "KiloV"}, {"W", "W"}, {"kW", "KiloW"}, {"MW", "MegaW"}, {"Wh", "W-HR"}, {"kWh", "KiloW-HR"}, {"rpm", "REV-PER-MIN"},
        {"Hz", "HZ"}, {"m3/h", "M3-PER-HR"}, {"l/min", "L-PER-MIN"}, {"l/s", "L-PER-SEC"}, {"m", "M"}, {"mm", "MilliM"},
        {"cm", "CentiM"}, {"m/s", "M-PER-SEC"}, {"mm/s", "MilliM-PER-SEC"}, {"km/h", "KiloM-PER-HR"}, {"s", "SEC"}, {"kg", "KiloGM"},
        {"l", "L"},
    }};
    for (const Row& r : kUnits) {
        if (r.unit == unit) return std::string(r.qudt);
    }
    return {};
}

namespace {

struct Object {
    enum class Kind { Iri, Plain, Typed, List } kind{Kind::Iri};
    std::string value;                 // IRI (prefixed or <full>) or lexical form
    std::string datatype;              // Typed
    std::vector<std::string> items;    // List: IRIs or quoted literals (rendered)
};

struct Triple {
    std::string s;
    std::string p;
    Object o;
};

std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

std::string render(const Object& o) {
    switch (o.kind) {
        case Object::Kind::Iri: return o.value;
        case Object::Kind::Plain: return "\"" + escape(o.value) + "\"";
        case Object::Kind::Typed: return "\"" + escape(o.value) + "\"^^" + o.datatype;
        case Object::Kind::List: {
            std::string s = "(";
            for (const std::string& i : o.items) s += " " + i;
            return s + " )";
        }
    }
    return {};
}

class Graph {
public:
    void iri(const std::string& s, const std::string& p, const std::string& o) { add(s, p, {Object::Kind::Iri, o, "", {}}); }
    void text(const std::string& s, const std::string& p, const std::string& o) { add(s, p, {Object::Kind::Plain, o, "", {}}); }
    void typed(const std::string& s, const std::string& p, const std::string& o, const std::string& dt) { add(s, p, {Object::Kind::Typed, o, dt, {}}); }
    void list(const std::string& s, const std::string& p, std::vector<std::string> items) { add(s, p, {Object::Kind::List, "", "", std::move(items)}); }

    void add(const std::string& s, const std::string& p, Object o) {
        const std::string key = s + "\x1f" + p + "\x1f" + render(o);
        if (keys_.insert(key).second) triples_.push_back({s, p, std::move(o)});
    }

    [[nodiscard]] std::vector<Triple> sorted() const {
        std::vector<Triple> t = triples_;
        std::sort(t.begin(), t.end(), [](const Triple& a, const Triple& b) {
            const std::string ra = render(a.o);
            const std::string rb = render(b.o);
            // rdf:type ("a") first within a subject, then predicates in order.
            const auto pk = [](const std::string& p) { return p == "a" ? std::string("\x01") : p; };
            return std::make_tuple(a.s, pk(a.p), ra) < std::make_tuple(b.s, pk(b.p), rb);
        });
        return t;
    }

    [[nodiscard]] std::size_t size() const noexcept { return triples_.size(); }

private:
    std::vector<Triple> triples_;
    std::set<std::string> keys_;
};

struct Prefix {
    std::string name;
    std::string iri;
};

std::vector<Prefix> prefixes(const Document& d) {
    return {{"", iri_base(d)},
            {"asset", iri_base(d) + "asset/"},
            {"owl", "http://www.w3.org/2002/07/owl#"},
            {"qudt", "http://qudt.org/schema/qudt/"},
            {"rdf", "http://www.w3.org/1999/02/22-rdf-syntax-ns#"},
            {"rdfs", "http://www.w3.org/2000/01/rdf-schema#"},
            {"sh", "http://www.w3.org/ns/shacl#"},
            {"sosa", "http://www.w3.org/ns/sosa/"},
            {"tw", "urn:vts:tw#"},
            {"unit", "http://qudt.org/vocab/unit/"},
            {"xsd", "http://www.w3.org/2001/XMLSchema#"}};
}

std::string turtle(const Document& d, const Graph& g, const std::string& title) {
    std::string out = "# " + title + " of the model " + (d.label.empty() ? d.name : d.label) + ", generated by " + std::string(kCompilerVersion) + ".\n";
    for (const Prefix& p : prefixes(d)) out += "@prefix " + p.name + ": <" + p.iri + "> .\n";
    std::string subject;
    std::string predicate;
    for (const Triple& t : g.sorted()) {
        if (t.s != subject) {
            if (!subject.empty()) out += " .\n";
            out += "\n" + t.s + " " + t.p + " " + render(t.o);
            subject = t.s;
            predicate = t.p;
        } else if (t.p != predicate) {
            out += " ;\n    " + t.p + " " + render(t.o);
            predicate = t.p;
        } else {
            out += ", " + render(t.o);
        }
    }
    if (!subject.empty()) out += " .\n";
    return out;
}

json::Json jsonld_value(const Object& o) {
    switch (o.kind) {
        case Object::Kind::Iri: return {{"@id", o.value}};
        case Object::Kind::Plain: return o.value;
        case Object::Kind::Typed: return {{"@value", o.value}, {"@type", o.datatype}};
        case Object::Kind::List: {
            json::Json items = json::Json::array();
            for (const std::string& i : o.items) {
                if (!i.empty() && i.front() == '"') {
                    items.push_back(i.substr(1, i.size() - 2));
                } else {
                    items.push_back({{"@id", i}});
                }
            }
            return {{"@list", items}};
        }
    }
    return nullptr;
}

json::Json jsonld(const Document& d, const Graph& schema, const Graph& graph) {
    json::Json ctx = json::Json::object();
    for (const Prefix& p : prefixes(d)) ctx[p.name.empty() ? "@vocab" : p.name] = p.iri;
    const auto nodes = [](const Graph& g) {
        json::Json out = json::Json::array();
        json::Json node;
        for (const Triple& t : g.sorted()) {
            if (node.is_null() || node["@id"] != t.s) {
                if (!node.is_null()) out.push_back(node);
                node = {{"@id", t.s}};
            }
            const std::string key = t.p == "a" ? "@type" : t.p;
            json::Json v = t.p == "a" ? json::Json(t.o.value) : jsonld_value(t.o);
            if (!node.contains(key)) node[key] = json::Json::array();
            node[key].push_back(v);
        }
        if (!node.is_null()) out.push_back(node);
        return out;
    };
    return {{"@context", ctx}, {"schema", {{"@graph", nodes(schema)}}}, {"graph", {{"@graph", nodes(graph)}}}};
}

std::string xsd(DataType t) {
    switch (t) {
        case DataType::Real: return "xsd:decimal";
        case DataType::Integer: return "xsd:integer";
        case DataType::Boolean: return "xsd:boolean";
        case DataType::Enum:
        case DataType::String: return "xsd:string";
    }
    return "xsd:string";
}

class Rdf {
public:
    explicit Rdf(const Resolved& r) : r_(r), d_(r.doc()) {}

    RdfOutput run() {
        vocabulary();
        for (const AssetType& t : d_.types) type(t);
        for (const RelationshipType& rt : d_.relationship_types) relationship_type(rt);
        if (r_.root() != nullptr) {
            for (const auto& inst : r_.instances()) asset(*inst);
            for (const auto& inst : r_.instances()) relationships(*inst);
        }
        RdfOutput out;
        out.schema_turtle = turtle(d_, schema_, "OWL 2 and SHACL schema");
        out.graph_turtle = turtle(d_, graph_, "Knowledge graph");
        out.jsonld = jsonld(d_, schema_, graph_);
        out.trace = std::move(trace_);
        out.schema_triples = schema_.size();
        out.graph_triples = graph_.size();
        return out;
    }

private:
    static std::string cls(const std::string& type) { return ":" + type; }
    static std::string member(const std::string& type, const std::string& m) { return ":" + type + "." + m; }

    std::string asset_iri(const Instance& i) const { return "asset:" + d_.name + (i.dotted.empty() ? "" : "." + i.dotted); }

    void traced(const std::string& kind, const std::string& name, const std::string& id, const char* rule) {
        if (!id.empty()) trace_.push_back(trace_entry(kind, name, {{id, rule}}));
    }

    void vocabulary() {
        const std::string onto = "<" + iri_base(d_) + ">";
        schema_.iri(onto, "a", "owl:Ontology");
        if (!d_.label.empty()) schema_.text(onto, "rdfs:label", d_.label);
        schema_.text(onto, "tw:compiler", std::string(kCompilerVersion));
        for (const char* c : {"tw:Asset", "tw:State", "tw:Event", "tw:Action", "tw:Transition", "tw:Signal"}) schema_.iri(c, "a", "owl:Class");
        schema_.iri("tw:hasPart", "a", "owl:ObjectProperty");
        schema_.iri("tw:hasPart", "owl:inverseOf", "tw:partOf");
        schema_.iri("tw:partOf", "a", "owl:ObjectProperty");
        schema_.iri("tw:references", "a", "owl:ObjectProperty");
    }

    void type(const AssetType& t) {
        const std::string c = cls(t.name);
        schema_.iri(c, "a", "owl:Class");
        schema_.iri(c, "rdfs:subClassOf", t.extends.empty() ? "tw:Asset" : cls(t.extends));
        schema_.text(c, "rdfs:label", t.label.empty() ? t.name : t.label);
        if (!t.description.empty()) schema_.text(c, "rdfs:comment", t.description);
        traced("class", c, t.id, t.extends.empty() ? "S1.class" : "S2.subclass");
        const std::string shape = c + "-shape";
        bool shaped = false;
        const auto shape_property = [&](const std::string& ps) {
            if (!shaped) {
                schema_.iri(shape, "a", "sh:NodeShape");
                schema_.iri(shape, "sh:targetClass", c);
                shaped = true;
            }
            schema_.iri(shape, "sh:property", ps);
        };
        for (const Property& p : t.properties) {
            const std::string m = member(t.name, p.name);
            schema_.text(m, "rdfs:label", p.label.empty() ? p.name : p.label);
            schema_.iri(m, "rdfs:domain", c);
            if (p.kind == PropertyKind::Measured) {
                schema_.iri(m, "a", "owl:ObjectProperty");
                schema_.iri(m, "rdfs:range", "sosa:ObservableProperty");
                if (const std::string u = qudt_unit(p.unit); !u.empty()) schema_.iri(m, "qudt:hasUnit", "unit:" + u);
                if (p.period) schema_.typed(m, "tw:period", *p.period, "xsd:decimal");
                if (p.min) schema_.typed(m, "tw:minimum", *p.min, "xsd:decimal");
                if (p.max) schema_.typed(m, "tw:maximum", *p.max, "xsd:decimal");
                traced("property", m, p.id, "S4.measured");
                continue;
            }
            schema_.iri(m, "a", "owl:DatatypeProperty");
            schema_.iri(m, "rdfs:range", xsd(p.type));
            if (const std::string u = qudt_unit(p.unit); !u.empty()) schema_.iri(m, "qudt:hasUnit", "unit:" + u);
            const std::string ps = m + "-shape";
            schema_.iri(ps, "a", "sh:PropertyShape");
            schema_.iri(ps, "sh:path", m);
            schema_.iri(ps, "sh:datatype", xsd(p.type));
            schema_.typed(ps, "sh:maxCount", "1", "xsd:integer");
            if (p.kind == PropertyKind::Fixed) schema_.typed(ps, "sh:minCount", "1", "xsd:integer");
            if (p.min) schema_.typed(ps, "sh:minInclusive", *p.min, xsd(p.type));
            if (p.max) schema_.typed(ps, "sh:maxInclusive", *p.max, xsd(p.type));
            if (p.type == DataType::Enum) {
                std::vector<std::string> items;
                for (const std::string& v : p.values) items.push_back("\"" + escape(v) + "\"");
                schema_.list(ps, "sh:in", items);
            }
            shape_property(ps);
            traced("property", m, p.id, "S3.datatype");
        }
        for (const Part& p : t.parts) {
            const std::string m = member(t.name, p.name);
            schema_.iri(m, "a", "owl:ObjectProperty");
            schema_.iri(m, "rdfs:subPropertyOf", p.external ? "tw:references" : "tw:hasPart");
            schema_.iri(m, "rdfs:domain", c);
            schema_.iri(m, "rdfs:range", cls(p.type));
            schema_.text(m, "rdfs:label", p.label.empty() ? p.name : p.label);
            const std::string ps = m + "-shape";
            schema_.iri(ps, "a", "sh:PropertyShape");
            schema_.iri(ps, "sh:path", m);
            schema_.iri(ps, "sh:class", cls(p.type));
            schema_.typed(ps, "sh:minCount", "1", "xsd:integer");
            schema_.typed(ps, "sh:maxCount", "1", "xsd:integer");
            shape_property(ps);
            traced("part", m, p.id, "S6.part");
        }
        for (const Event& e : t.events) {
            const std::string m = member(t.name, e.name);
            schema_.iri(m, "a", "tw:Event");
            schema_.iri(m, "tw:ofType", c);
            schema_.text(m, "rdfs:label", e.label.empty() ? e.name : e.label);
            schema_.text(m, "tw:origin", e.origin == EventOrigin::Condition ? "condition" : "reported");
            if (!e.when.empty()) schema_.text(m, "tw:condition", e.when);
            if (!e.meaning.empty()) schema_.text(m, "tw:meaning", e.meaning);
            traced("event", m, e.id, "S7.event");
        }
        for (const Action& a : t.actions) {
            const std::string m = member(t.name, a.name);
            schema_.iri(m, "a", "tw:Action");
            schema_.iri(m, "tw:ofType", c);
            schema_.text(m, "rdfs:label", a.label.empty() ? a.name : a.label);
            if (!a.meaning.empty()) schema_.text(m, "tw:meaning", a.meaning);
            traced("action", m, a.id, "S7.action");
        }
        if (t.behavior) {
            for (const State& s : t.behavior->states) {
                const std::string m = member(t.name, "state." + s.name);
                schema_.iri(m, "a", "tw:State");
                schema_.iri(m, "tw:ofType", c);
                schema_.text(m, "rdfs:label", s.label.empty() ? s.name : s.label);
                if (s.initial) schema_.typed(m, "tw:initial", "true", "xsd:boolean");
                if (!s.meaning.empty()) schema_.text(m, "tw:meaning", s.meaning);
                traced("state", m, s.id, "S7.state");
            }
            for (const Transition& tr : t.behavior->transitions) {
                const std::string m = member(t.name, "transition." + tr.id);
                schema_.iri(m, "a", "tw:Transition");
                schema_.iri(m, "tw:ofType", c);
                for (const std::string& f : tr.from) schema_.iri(m, "tw:from", member(t.name, "state." + f));
                schema_.iri(m, "tw:to", member(t.name, "state." + tr.to));
                switch (tr.trigger.kind) {
                    case TriggerKind::Event:
                    case TriggerKind::Action: schema_.text(m, "tw:trigger", tr.trigger.ref); break;
                    case TriggerKind::Timeout: {
                        const auto* i = std::get_if<std::int64_t>(&tr.trigger.after.value);
                        schema_.text(m, "tw:after", i != nullptr ? std::to_string(*i) : std::get<std::string>(tr.trigger.after.value));
                        break;
                    }
                    case TriggerKind::Internal: schema_.text(m, "tw:trigger", "internal"); break;
                }
                traced("transition", m, tr.id, "S7.transition");
            }
        }
    }

    void relationship_type(const RelationshipType& rt) {
        const std::string m = ":" + rt.name;
        schema_.iri(m, "a", "owl:ObjectProperty");
        schema_.text(m, "rdfs:label", rt.label.empty() ? rt.name : rt.label);
        if (rt.symmetric) schema_.iri(m, "a", "owl:SymmetricProperty");
        if (!rt.inverse.empty()) {
            schema_.iri(m, "owl:inverseOf", ":" + rt.inverse);
            schema_.iri(":" + rt.inverse, "a", "owl:ObjectProperty");
        }
        if (rt.from.size() == 1) schema_.iri(m, "rdfs:domain", cls(rt.from.front()));
        if (rt.to.size() == 1) schema_.iri(m, "rdfs:range", cls(rt.to.front()));
        const auto ends = [&](const std::vector<std::string>& types, const char* target, const std::string& shape) {
            if (types.empty()) return;
            schema_.iri(shape, "a", "sh:NodeShape");
            schema_.iri(shape, target, m);
            if (types.size() == 1) {
                schema_.iri(shape, "sh:class", cls(types.front()));
                return;
            }
            std::vector<std::string> alts;
            for (const std::string& t : types) {
                const std::string is = cls(t) + "-isa";
                schema_.iri(is, "a", "sh:NodeShape");
                schema_.iri(is, "sh:class", cls(t));
                alts.push_back(is);
            }
            schema_.list(shape, "sh:or", alts);
        };
        ends(rt.from, "sh:targetSubjectsOf", m + "-from-shape");
        ends(rt.to, "sh:targetObjectsOf", m + "-to-shape");
        traced("relationship-type", m, rt.id, "S5.relationship");
    }

    void asset(const Instance& i) {
        const std::string a = asset_iri(i);
        const std::string t = i.type->type->name;
        graph_.iri(a, "a", cls(t));
        std::string label = i.part != nullptr ? (i.part->label.empty() ? i.part->name : i.part->label) : (d_.label.empty() ? d_.name : d_.label);
        graph_.text(a, "rdfs:label", label);
        graph_.text(a, "tw:path", i.dotted);
        traced("asset", a, i.part != nullptr ? i.part->id : i.type->type->id, "G1.asset");
        if (i.parent != nullptr) {
            const std::string owner_type = i.parent->type->type->name;
            const AssetType* declaring = nullptr;
            for (const AssetType* x : i.parent->type->lineage) {
                for (const Part& p : x->parts) {
                    if (&p == i.part) declaring = x;
                }
            }
            graph_.iri(asset_iri(*i.parent), member(declaring != nullptr ? declaring->name : owner_type, i.part->name), a);
            graph_.iri(asset_iri(*i.parent), i.external ? "tw:references" : "tw:hasPart", a);
        }
        for (const Property* p : i.type->properties) {
            const AssetType* declaring = nullptr;
            for (const AssetType* x : i.type->lineage) {
                for (const Property& q : x->properties) {
                    if (&q == p) declaring = x;
                }
            }
            const std::string m = member(declaring != nullptr ? declaring->name : t, p->name);
            if (p->kind == PropertyKind::Measured) {
                const std::string o = a + "." + p->name;
                graph_.iri(a, m, o);
                graph_.iri(o, "a", "sosa:ObservableProperty");
                graph_.iri(o, "a", "tw:Signal");
                graph_.text(o, "tw:symbol", symbol_of(i, *p));
                traced("signal", o, p->id, "G5.signal");
            } else if (p->kind == PropertyKind::Fixed) {
                if (const auto v = r_.value(i, *p)) graph_.typed(a, m, *v, xsd(p->type));
            }
        }
    }

    void relationships(const Instance& i) {
        if (i.external) return;
        for (const Relationship* rel : i.type->relationships) {
            const Instance* from = r_.part(i, rel->from);
            const Instance* to = r_.part(i, rel->to);
            if (from == nullptr || to == nullptr) continue;
            graph_.iri(asset_iri(*from), ":" + rel->type, asset_iri(*to));
            traced("relationship", asset_iri(*from) + " :" + rel->type + " " + asset_iri(*to), rel->id, "G3.relationship");
        }
    }

    const Resolved& r_;
    const Document& d_;
    Graph schema_;
    Graph graph_;
    json::Json trace_ = json::Json::array();
};

}  // namespace

RdfOutput compile_rdf(const Resolved& model) { return Rdf(model).run(); }

}  // namespace twin::model::detail
