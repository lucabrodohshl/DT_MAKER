/**
 * @file model_command.cpp
 * @brief `twin model check|compile|lift`: the twin modelling language on the command line.
 */
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "cli.hpp"
#include "commands.hpp"
#include "twin/authoring/import.hpp"
#include "twin/json/canonical.hpp"
#include "twin/model/compile.hpp"
#include "twin/model/lift.hpp"

namespace twin::cli {
namespace {

constexpr const char* kHelp =
    "twin model check <model.json>\n"
    "twin model compile <model.json> [--out <dir>]\n"
    "twin model lift <automaton.xml|.tta.json|.tta> [--ontology <K.ont>] [--interp <I.interp>]\n"
    "                [--name <model-name>] [--out <model.json>]\n"
    "twin model reference <model.json> --automaton <pt.xml|.tta.json> [--map <map.json>] [--out <model.json>]\n\n"
    "check    validates a twin-model/1 document and prints its diagnostics.\n"
    "compile  derives every artefact: <name>.tta.json and <name>.xml (V_D), <name>.ont (K),\n"
    "         <name>.dt.interp (I_D), <name>.pt.xml and <name>.pt.interp (with a physical\n"
    "         reference), <name>.schema.ttl, <name>.graph.ttl, <name>.jsonld, <name>.sections.json\n"
    "         and <name>.trace.json (default dir: .).\n"
    "lift     turns a timed automaton (and its ontology and interpretation) into a model.\n"
    "reference attaches a physical reference (V_P) to a model; locations and channels with the\n"
    "         same names as states, events and actions are mapped, --map adds or overrides\n"
    "         {\"states\": {LOC: [state paths] | {\"meaning\": expr}}, \"labels\": {CHAN: trigger | \"internal\"}}.\n"
    "Exit code 0: no errors; 1: the model has errors; 2: usage; 3: unreadable input.";

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

bool write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    return static_cast<bool>(out);
}

void print(const std::vector<model::Diagnostic>& diagnostics) {
    for (const model::Diagnostic& d : diagnostics) {
        std::cerr << d.severity << "[" << d.code << "] " << d.path << ": " << d.message;
        if (!d.hint.empty()) std::cerr << " (hint: " << d.hint << ")";
        std::cerr << "\n";
    }
}

std::optional<json::Json> load_model(const std::string& path) {
    const auto text = read_file(path);
    if (!text) {
        std::cerr << "error: cannot read " << path << "\n";
        return std::nullopt;
    }
    auto j = json::parse(*text);
    if (!j) {
        std::cerr << "error: " << path << " is not JSON: " << j.error().message << "\n";
        return std::nullopt;
    }
    return std::move(j).value();
}

std::string pretty(const json::Json& j) { return j.dump(2) + "\n"; }

int check(const Args& args) {
    if (args.positionals().size() != 2) return usage_error("expected a model file", kHelp);
    const auto doc = load_model(args.positionals()[1]);
    if (!doc) return kInputError;
    const auto diags = model::check(*doc);
    print(diags);
    if (model::has_errors(diags)) return kFailure;
    std::cout << "model is valid\n";
    return kOk;
}

int compile(const Args& args) {
    if (args.positionals().size() != 2) return usage_error("expected a model file", kHelp);
    const auto doc = load_model(args.positionals()[1]);
    if (!doc) return kInputError;
    const model::Compiled c = model::compile(*doc);
    print(c.diagnostics);
    if (!c.artifacts) {
        std::cerr << "compilation failed\n";
        return kFailure;
    }
    const model::Artifacts& a = *c.artifacts;
    const std::filesystem::path out = args.option_or("out", ".");
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    const std::string name = doc->value("name", std::string("model"));
    std::vector<std::pair<std::string, std::string>> files = {
        {name + ".ont", a.ontology},
        {name + ".dt.interp", a.dt_interpretation},
        {name + ".schema.ttl", a.schema_turtle},
        {name + ".graph.ttl", a.graph_turtle},
        {name + ".jsonld", pretty(a.graph_jsonld)},
        {name + ".sections.json", pretty(a.sections)},
        {name + ".trace.json", pretty(a.trace)},
    };
    if (a.automaton) {
        files.emplace_back(name + ".tta.json", pretty(authoring::to_json(*a.automaton)));
        files.emplace_back(name + ".xml", a.automaton_xml);
    }
    if (a.reference) {
        files.emplace_back(name + ".pt.xml", a.reference_xml);
        files.emplace_back(name + ".pt.interp", a.pt_interpretation);
    }
    for (const auto& [file, content] : files) {
        if (!write_file(out / file, content)) {
            std::cerr << "error: cannot write " << (out / file).string() << "\n";
            return kInputError;
        }
    }
    std::cout << "compiled " << name << " (" << c.semantic_digest.substr(0, 12) << ") -> " << out.string() << "\n";
    for (const auto& [k, v] : a.summary.items()) {
        if (k != "compiler" && k != "digest") std::cout << "  " << k << ": " << v.dump() << "\n";
    }
    return kOk;
}

int lift(const Args& args) {
    if (args.positionals().size() != 2) return usage_error("expected an automaton file", kHelp);
    const std::string path = args.positionals()[1];
    const auto text = read_file(path);
    if (!text) {
        std::cerr << "error: cannot read " << path << "\n";
        return kInputError;
    }
    authoring::ImportOptions options;
    options.filename = std::filesystem::path(path).filename().string();
    options.legacy_system_declaration = args.flag("legacy-system-declaration");
    const authoring::ImportResult imported = authoring::import_any(*text, options);
    if (!imported.model) {
        for (const auto& d : imported.diagnostics) std::cerr << d.severity << "[" << d.code << "] " << d.message << "\n";
        std::cerr << "error: " << path << " is not a timed automaton in the supported fragment\n";
        return kFailure;
    }
    model::LiftInput in;
    in.automaton = *imported.model;
    in.name = args.option_or("name", "");
    for (const auto& [option, field] : {std::pair{"ontology", &in.ontology}, std::pair{"interp", &in.interpretation}}) {
        if (const auto f = args.option(option)) {
            const auto t = read_file(*f);
            if (!t) {
                std::cerr << "error: cannot read " << *f << "\n";
                return kInputError;
            }
            *field = *t;
        }
    }
    const model::Lifted lifted = model::lift(in);
    print(lifted.diagnostics);
    if (!lifted.document) return kFailure;
    const std::string doc = pretty(model::encode(*lifted.document));
    if (const auto out = args.option("out")) {
        if (!write_file(*out, doc)) {
            std::cerr << "error: cannot write " << *out << "\n";
            return kInputError;
        }
        std::cout << "lifted " << in.automaton.name << " -> " << *out << "\n";
    } else {
        std::cout << doc;
    }
    return kOk;
}


std::optional<authoring::Model> import_automaton(const std::string& path, bool legacy) {
    const auto text = read_file(path);
    if (!text) {
        std::cerr << "error: cannot read " << path << "\n";
        return std::nullopt;
    }
    authoring::ImportOptions options;
    options.filename = std::filesystem::path(path).filename().string();
    options.legacy_system_declaration = legacy;
    authoring::ImportResult imported = authoring::import_any(*text, options);
    if (!imported.model) {
        for (const auto& d : imported.diagnostics) std::cerr << d.severity << "[" << d.code << "] " << d.message << "\n";
        std::cerr << "error: " << path << " is not a timed automaton in the supported fragment\n";
        return std::nullopt;
    }
    return std::move(*imported.model);
}

int reference(const Args& args) {
    if (args.positionals().size() != 2 || !args.option("automaton")) return usage_error("expected a model file and --automaton", kHelp);
    auto doc = load_model(args.positionals()[1]);
    if (!doc) return kInputError;
    model::Decoded decoded = model::decode(*doc);
    print(decoded.diagnostics);
    if (model::has_errors(decoded.diagnostics)) return kFailure;
    const auto pt = import_automaton(*args.option("automaton"), args.flag("legacy-system-declaration"));
    if (!pt) return kFailure;
    model::Reference ref = model::reference_suggestion(decoded.document, *pt);
    if (const auto map_path = args.option("map")) {
        const auto map = load_model(*map_path);
        if (!map) return kInputError;
        if (map->contains("states")) {
            for (const auto& [loc, v] : map->at("states").items()) {
                model::ReferenceState rs;
                if (v.is_array()) {
                    for (const auto& s : v) rs.states.push_back(s.get<std::string>());
                } else if (v.is_object()) {
                    rs.meaning = v.value("meaning", std::string());
                } else if (v.is_string()) {
                    rs.states.push_back(v.get<std::string>());
                }
                ref.states[loc] = rs;
            }
        }
        if (map->contains("labels")) {
            for (const auto& [chan, v] : map->at("labels").items()) {
                const std::string t = v.is_string() ? v.get<std::string>() : std::string();
                ref.labels[chan] = t == "internal" ? model::ReferenceLabel{"", true} : model::ReferenceLabel{t, false};
            }
        }
    }
    decoded.document.reference = std::move(ref);
    const std::string out_doc = pretty(model::encode(decoded.document));
    const std::string out = args.option_or("out", args.positionals()[1]);
    if (!write_file(out, out_doc)) {
        std::cerr << "error: cannot write " << out << "\n";
        return kInputError;
    }
    std::cout << "reference " << pt->name << " attached -> " << out << "\n";
    return kOk;
}

}  // namespace

int run_model(int argc, char** argv) {
    Args args(argc, argv, 0, {"out", "ontology", "interp", "name", "automaton", "map"});
    if (args.error()) return usage_error(*args.error(), kHelp);
    if (args.flag("help") || args.positionals().empty()) {
        std::cout << kHelp << "\n";
        return args.positionals().empty() && !args.flag("help") ? kUsageError : kOk;
    }
    const std::string sub = args.positionals()[0];
    if (sub == "check") return check(args);
    if (sub == "compile") return compile(args);
    if (sub == "lift") return lift(args);
    if (sub == "reference") return reference(args);
    return usage_error("unknown subcommand '" + sub + "'", kHelp);
}

}  // namespace twin::cli
