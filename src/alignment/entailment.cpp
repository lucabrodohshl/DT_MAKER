/**
 * @file entailment.cpp
 * @brief Ontology entailment queries through the aligner's parsers and dtpta::Ontology.
 */
#include "twin/alignment/entailment.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "capture.hpp"
#include "dtpta/domain_parser.h"
#include "dtpta/interpretation.h"

namespace twin::alignment {
namespace {

namespace fs = std::filesystem;

/// A scratch directory removed on destruction.
class Scratch {
public:
    Scratch() {
        static std::atomic<std::uint64_t> counter{0};
        dir_ = fs::temp_directory_path() /
               ("twin-entail-" + std::to_string(::getpid()) + "-" + std::to_string(counter.fetch_add(1)));
        fs::create_directories(dir_);
    }
    ~Scratch() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    fs::path write(const std::string& name, const std::string& text) const {
        const fs::path p = dir_ / name;
        std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
        return p;
    }

private:
    fs::path dir_;
};

std::string one_line(std::string s) {
    for (char& c : s) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    return s;
}

}  // namespace

Status check_formula(const std::string& ontology_text, const std::string& formula) {
    try {
        detail::CoutCapture capture;
        Scratch s;
        dtpta::OntFileParser ont_parser;
        dtpta::InterpFileParser interp_parser;
        std::shared_ptr<dtpta::Ontology> ontology = ont_parser.parse(s.write("k.ont", ontology_text).string());
        const dtpta::InterpretationMap q =
            interp_parser.parse(s.write("q.interp", "__twin_q : " + one_line(formula) + "\n").string(), ontology);
        if (!q.get_state("__twin_q")) return make_error(ErrorCode::ParseError, "the formula could not be parsed");
        return ok_status();
    } catch (const std::exception& e) {
        return make_error(ErrorCode::ParseError, std::string("the formula is not well formed over the ontology: ") + e.what());
    }
}

Result<std::map<std::string, std::optional<bool>>> entailment_by_location(
    const std::string& ontology_text, const std::string& interpretation_text,
    const std::map<std::string, std::string>& formula_by_location) {
    try {
        detail::CoutCapture capture;
        Scratch s;
        dtpta::OntFileParser ont_parser;
        dtpta::InterpFileParser interp_parser;
        std::shared_ptr<dtpta::Ontology> ontology = ont_parser.parse(s.write("k.ont", ontology_text).string());
        const dtpta::InterpretationMap interp = interp_parser.parse(s.write("i.interp", interpretation_text).string(), ontology);
        std::string queries;
        std::map<std::string, std::string> names;
        std::size_t n = 0;
        for (const auto& [location, formula] : formula_by_location) {
            const std::string name = "__twin_q" + std::to_string(n++);
            names[location] = name;
            queries += name + " : " + one_line(formula) + "\n";
        }
        const dtpta::InterpretationMap q = interp_parser.parse(s.write("q.interp", queries).string(), ontology);
        std::map<std::string, std::optional<bool>> out;
        for (const auto& [location, name] : names) {
            const std::optional<z3::expr> meaning = interp.get_state(location);
            const std::optional<z3::expr> phi = q.get_state(name);
            if (!phi) return make_error(ErrorCode::ParseError, "the formula for '" + location + "' could not be parsed");
            if (!meaning) {
                out[location] = std::nullopt;
                continue;
            }
            out[location] = ontology->entails(z3::implies(*meaning, *phi));
        }
        return out;
    } catch (const std::exception& e) {
        return make_error(ErrorCode::ParseError, std::string("the ontology or interpretation could not be read: ") + e.what());
    }
}

}  // namespace twin::alignment
