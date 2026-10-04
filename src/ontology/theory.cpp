/**
 * @file theory.cpp
 * @brief The aligner's Z3 reading of an ontology (see theory.hpp).
 */
#include "theory.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <charconv>
#include <system_error>

#include "dtpta/domain_parser.h"
#include "dtpta/ontology.h"
#include "twin/alignment/aligner_identity.hpp"

namespace twin::ontology {

std::string_view to_string(Verdict3 v) noexcept {
    switch (v) {
        case Verdict3::True: return "true";
        case Verdict3::False: return "false";
        case Verdict3::Unknown: return "unknown";
    }
    return "unknown";
}

std::string checker_identity() { return detail::checker_identity(); }

namespace detail {

namespace {

namespace fs = std::filesystem;

/**
 * @brief A uniquely named temporary file, removed on destruction.
 *
 * The aligner's parsers only read files, so texts are handed over through
 * the file system. Files live in the system temp directory and contain only
 * the ontology/formula text.
 */
class TempFile {
public:
    TempFile(std::string_view suffix, std::string_view content) {
        static std::atomic<unsigned long long> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
                ("twin-ontology-" + std::to_string(stamp) + "-" + std::to_string(counter.fetch_add(1)) +
                 std::string(suffix));
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        ok_ = static_cast<bool>(out);
    }
    ~TempFile() {
        std::error_code ec;
        fs::remove(path_, ec);
    }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }
    [[nodiscard]] bool ok() const noexcept { return ok_; }

private:
    fs::path path_;
    bool ok_{false};
};

/// @brief Render a model as sorted (symbol, value) pairs.
Model render_model(const z3::model& m) {
    Model out;
    for (unsigned i = 0; i < m.size(); ++i) {
        z3::func_decl d = m[static_cast<int>(i)];
        std::string value;
        if (d.arity() == 0) {
            z3::expr v = m.get_const_interp(d);
            value = v.is_numeral() ? v.get_decimal_string(6) : v.to_string();
            // Exact rationals are clearer than truncated decimals when they are short.
            if (v.is_numeral() && v.to_string().size() < value.size()) value = v.to_string();
        } else {
            // Finite table "[args -> value, ..., else -> value]".
            z3::func_interp fi = m.get_func_interp(d);
            std::ostringstream ss;
            ss << '[';
            for (unsigned k = 0; k < fi.num_entries(); ++k) {
                z3::func_entry entry = fi.entry(k);
                for (unsigned a = 0; a < entry.num_args(); ++a) ss << (a > 0 ? " " : "") << entry.arg(a).to_string();
                ss << " -> " << entry.value().to_string() << ", ";
            }
            ss << "else -> " << fi.else_value().to_string() << ']';
            value = ss.str();
        }
        out.emplace_back(d.name().str(), value);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

std::string checker_identity() {
    unsigned major = 0;
    unsigned minor = 0;
    unsigned build = 0;
    unsigned revision = 0;
    Z3_get_version(&major, &minor, &build, &revision);
    std::ostringstream ss;
    ss << "z3 " << major << '.' << minor << '.' << build << "; " << twin::alignment::kAlignerName << ' '
       << twin::alignment::kAlignerSourceDigest.substr(0, 16) << "; " << kOntologyServicesVersion;
    return ss.str();
}

Theory::Theory(std::shared_ptr<dtpta::Ontology> ontology) : ontology_(std::move(ontology)) {}

Theory::~Theory() {
    axioms_.clear();  // expressions must die before the context owned by ontology_
}

z3::context& Theory::context() { return ontology_->get_context(); }

Result<std::unique_ptr<Theory>> Theory::load(std::string_view ontology_text) {
    TempFile file(".ont", ontology_text);
    if (!file.ok()) return make_error(ErrorCode::IoError, "cannot write temporary ontology file");
    try {
        dtpta::OntFileParser parser;
        auto ontology = parser.parse(file.path().string());
        return std::unique_ptr<Theory>(new Theory(std::move(ontology)));
    } catch (const z3::exception& e) {
        return make_error(ErrorCode::ParseError, std::string("aligner (Z3) rejected the ontology: ") + e.msg());
    } catch (const std::exception& e) {
        return make_error(ErrorCode::ParseError, std::string("aligner rejected the ontology: ") + e.what());
    }
}

Result<std::vector<z3::expr>> Theory::parse_formulas(const std::vector<std::string>& formulas) {
    std::vector<z3::expr> out;
    if (formulas.empty()) return out;
    // One interpretation line per formula, keyed f<i>. The keys are location-style
    // (no '!'), so InterpFileParser stores them as state formulas.
    std::string text;
    for (std::size_t i = 0; i < formulas.size(); ++i) {
        if (formulas[i].find('\n') != std::string::npos) {
            return make_error(ErrorCode::InvalidArgument, "formula must be a single line")
                .with("index", std::to_string(i));
        }
        text += "f" + std::to_string(i) + " : " + formulas[i] + "\n";
    }
    TempFile file(".interp", text);
    if (!file.ok()) return make_error(ErrorCode::IoError, "cannot write temporary formula file");
    try {
        dtpta::InterpFileParser parser;
        dtpta::InterpretationMap map = parser.parse(file.path().string(), ontology_);
        out.reserve(formulas.size());
        for (std::size_t i = 0; i < formulas.size(); ++i) {
            auto e = map.get_state("f" + std::to_string(i));
            if (!e) {
                return make_error(ErrorCode::ParseError, "formula was not read by the aligner: " + formulas[i])
                    .with("index", std::to_string(i));
            }
            if (!e->is_bool()) {
                return make_error(ErrorCode::ValidationError, "formula is not Boolean: " + formulas[i])
                    .with("index", std::to_string(i));
            }
            out.push_back(*e);
        }
        return out;
    } catch (const z3::exception& e) {
        return make_error(ErrorCode::ParseError, std::string("Z3 rejected a formula: ") + e.msg());
    } catch (const std::exception& e) {
        // InterpFileParser names the failing key (f<i>); map it back to the formula.
        std::string message = e.what();
        Error err = make_error(ErrorCode::ParseError, message);
        const auto pos = message.find("'f");
        if (pos != std::string::npos) {
            const auto end = message.find('\'', pos + 2);
            const std::string index = message.substr(pos + 2, end - pos - 2);
            err.with("index", index);
            std::size_t i = 0;
            const auto [ptr, ec] = std::from_chars(index.data(), index.data() + index.size(), i);
            if (ec == std::errc{} && ptr == index.data() + index.size() && i < formulas.size()) {
                err.message = "formula does not type-check against the ontology signature: " + formulas[i] +
                              " (aligner: " + message + ")";
            }
        }
        return err;
    }
}

Result<std::vector<std::pair<std::string, z3::expr>>> Theory::read_interpretation(std::string_view text) {
    TempFile file(".interp", text);
    if (!file.ok()) return make_error(ErrorCode::IoError, "cannot write temporary interpretation file");
    try {
        dtpta::InterpFileParser parser;
        dtpta::InterpretationMap map = parser.parse(file.path().string(), ontology_);
        std::vector<std::pair<std::string, z3::expr>> out;
        for (const auto& name : map.state_names()) out.emplace_back(name, *map.get_state(name));
        for (const auto& label : map.event_labels()) out.emplace_back(label, *map.get_event(label));
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        return out;
    } catch (const z3::exception& e) {
        return make_error(ErrorCode::ParseError, std::string("Z3 rejected the interpretation: ") + e.msg());
    } catch (const std::exception& e) {
        return make_error(ErrorCode::ParseError, std::string("aligner rejected the interpretation: ") + e.what());
    }
}

Result<z3::expr> Theory::parse_formula(const std::string& formula) {
    auto r = parse_formulas({formula});
    if (!r) return std::move(r).error();
    return r.value().front();
}

Status Theory::set_axioms(const std::vector<std::string>& axiom_formulas) {
    auto parsed = parse_formulas(axiom_formulas);
    if (!parsed) return std::move(parsed).error();
    axioms_ = std::move(parsed).value();
    return {};
}

Entailment Theory::check(const std::vector<z3::expr>& extra, const z3::expr* negated_goal, unsigned timeout_ms) {
    Entailment result;
    try {
        z3::context& ctx = context();
        z3::solver solver(ctx);
        z3::params p(ctx);
        p.set("timeout", timeout_ms);
        solver.set(p);
        for (const auto& a : axioms_) solver.add(a);
        for (const auto& a : extra) solver.add(a);
        if (negated_goal != nullptr) solver.add(!*negated_goal);
        switch (solver.check()) {
            case z3::unsat:
                result.verdict = Verdict3::True;
                break;
            case z3::sat:
                result.verdict = Verdict3::False;
                result.counter_model = render_model(solver.get_model());
                break;
            case z3::unknown:
                result.verdict = Verdict3::Unknown;
                result.reason = solver.reason_unknown();
                break;
        }
    } catch (const z3::exception& e) {
        result.verdict = Verdict3::Unknown;
        result.reason = std::string("solver error: ") + e.msg();
    }
    return result;
}

Entailment Theory::entails(const z3::expr& phi, unsigned timeout_ms) { return check({}, &phi, timeout_ms); }

Entailment Theory::entails_under(const std::vector<z3::expr>& extra, const z3::expr& phi, unsigned timeout_ms) {
    return check(extra, &phi, timeout_ms);
}

Verdict3 Theory::satisfiable(const std::vector<z3::expr>& extra, unsigned timeout_ms, std::string* reason) {
    // check() reports "goal entailed" as True; without a goal: unsat -> True means
    // "Δ ∪ extra ⊨ false", i.e. NOT satisfiable. Translate.
    Entailment e = check(extra, nullptr, timeout_ms);
    if (reason != nullptr) *reason = e.reason;
    switch (e.verdict) {
        case Verdict3::True: return Verdict3::False;
        case Verdict3::False: return Verdict3::True;
        case Verdict3::Unknown: return Verdict3::Unknown;
    }
    return Verdict3::Unknown;
}

std::vector<std::string> Theory::symbol_names() const {
    std::vector<std::string> out;
    for (const auto& f : ontology_->get_functions()) out.push_back(f.name);
    for (const auto& r : ontology_->get_relations()) out.push_back(r.name);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> Theory::sort_names() const {
    std::vector<std::string> out;
    for (const auto& s : ontology_->get_sorts()) out.push_back(s.name);
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace detail
}  // namespace twin::ontology
