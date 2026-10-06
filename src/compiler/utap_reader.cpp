/**
 * @file utap_reader.cpp
 * @brief Strict reader: UTAP abstract syntax -> SourceModel, rejecting everything
 *        outside the fragment to which the semantic aligner gives semantics.
 *
 * Each rejection documents *why* the construct is refused. In most cases the
 * existing aligner would not fail on it but silently change the model (e.g.
 * treat a disjunction as a conjunction, or drop an unparseable guard), so the
 * verified model would differ from the source. See
 * docs/existing-aligner-integration.md, section "Silent weakenings".
 */
#include "utap_reader.hpp"

#include "twin/compiler/compiler.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "dbm/constraints.h"
#include "utap/utap.hpp"

namespace twin::compiler::detail {
namespace {

using UTAP::Constants::Kind;

constexpr const char* kDocs = "docs/supported-model-fragment.md";

class StrictReader {
public:
    StrictReader(UTAP::Document& doc, const ReaderOptions& options, std::vector<Diagnostic>& diagnostics)
        : doc_(doc), options_(options), diags_(diagnostics) {}

    std::optional<SourceModel> run() {
        if (!check_document()) {
            return std::nullopt;
        }
        UTAP::Template& tmpl = doc_.get_templates().front();
        model_.template_name = tmpl.uid.get_name();
        read_declarations(doc_.get_globals(), "global declarations", /*globals=*/true);
        read_declarations(tmpl, "template " + model_.template_name, /*globals=*/false);
        check_template_shape(tmpl);
        read_locations(tmpl);
        read_edges(tmpl);
        if (error_count_ > 0) {
            return std::nullopt;
        }
        return std::move(model_);
    }

private:
    void error(std::string code, std::string where, std::string message, std::string hint = {}) {
        diags_.push_back(Diagnostic{Severity::Error, std::move(code), std::move(message),
                                    std::move(where), std::move(hint)});
        ++error_count_;
    }
    void warning(std::string code, std::string where, std::string message) {
        diags_.push_back(Diagnostic{Severity::Warning, std::move(code), std::move(message),
                                    std::move(where), {}});
    }

    // ---------------------------------------------------------------- document
    bool check_document() {
        for (const UTAP::Error& e : doc_.get_errors()) {
            const std::string path = e.start.path ? *e.start.path : std::string();
            const std::string where = (path.empty() ? std::string("document") : path) + " line " +
                                      std::to_string(e.start.line);
            const std::string what = e.msg + (e.context.empty() ? "" : " (" + e.context + ")");
            if (in_system_declaration(path) && options_.legacy_system_declaration) {
                warning("TWC015", where,
                        "invalid system declaration ignored (legacy mode, as the aligner does): " + what);
                system_declaration_ignored_ = true;
                continue;
            }
            error("TWC001", where, "UPPAAL syntax/type error: " + what,
                  in_system_declaration(path)
                      ? "UPPAAL rejects this document; fix the <system> declaration, or pass "
                        "--legacy-system-declaration to compile the single template as the aligner does"
                      : "the semantic aligner ignores UTAP type errors; the compiler does not");
        }
        for (const UTAP::Error& w : doc_.get_warnings()) {
            warning("TWC002", "line " + std::to_string(w.start.line), "UPPAAL warning: " + w.msg);
        }
        if (error_count_ > 0) {
            return false;
        }
        const auto& templates = doc_.get_templates();
        if (templates.size() != 1) {
            error("TWC003", "document",
                  "exactly one template is required, found " + std::to_string(templates.size()),
                  "the aligner analyses only the first template of a document; split the "
                  "views into separate documents");
            return false;
        }
        const UTAP::Template& tmpl = templates.front();
        if (!tmpl.is_TA) {
            error("TWC004", "template " + tmpl.uid.get_name(), "only timed-automaton templates are supported");
        }
        auto& processes = doc_.get_processes();
        if (!system_declaration_ignored_ &&
            (processes.size() != 1 || processes.front().templ != &tmpl)) {
            error("TWC005", "system declaration",
                  "the system must instantiate the single template exactly once",
                  "use: process P = " + tmpl.uid.get_name() + "(); system P;");
        }
        if (doc_.has_priority_declaration()) {
            error("TWC006", "system declaration", "channel/process priorities are not supported");
        }
        return error_count_ == 0;
    }

    void check_template_shape(const UTAP::Template& tmpl) {
        const std::string where = "template " + model_.template_name;
        if (tmpl.parameters.get_size() > 0) {
            error("TWC007", where, "template parameters are not supported",
                  "the aligner treats parameters as uninitialised variables; inline the values");
        }
        if (!tmpl.functions.empty() || !doc_.get_globals().functions.empty()) {
            error("TWC008", where, "user-defined functions are not supported",
                  "the aligner does not evaluate function bodies");
        }
        if (!tmpl.branchpoints.empty()) {
            error("TWC009", where, "branchpoints are not supported");
        }
        if (tmpl.locations.empty()) {
            error("TWC010", where, "the template has no locations");
        }
    }

    // ------------------------------------------------------------- declarations
    static bool in_system_declaration(const std::string& path) {
        return path.rfind("/nta/system", 0) == 0;
    }

    template <class Decls>
    void read_declarations(Decls& decls, const std::string& where, bool globals) {
        bool in_prelude = globals;  // UTAP prepends its built-in declarations to the globals
        for (UTAP::Variable& v : decls.variables) {
            const std::string name = v.uid.get_name();
            if (in_prelude && utap_builtin_names().contains(name)) {
                continue;
            }
            in_prelude = false;
            const UTAP::Type& type = v.uid.get_type();
            const std::string at = where + ", declaration of '" + name + "'";
            if (type.is_array()) {
                error("TWC011", at, "arrays are not supported (" + type.str() + ")",
                      "the aligner names array channels/clocks textually and cannot index them");
                continue;
            }
            if (type.is_clock()) {
                model_.clocks.push_back(name);
                clock_index_[name] = static_cast<ir::ClockIndex>(model_.clocks.size());
                continue;
            }
            if (type.is_channel()) {
                if (type.is(Kind::URGENT) || type.is(Kind::BROADCAST)) {
                    error("TWC012", at, "urgent and broadcast channels are not supported",
                          "the aligner ignores channel kinds, which would change the semantics");
                    continue;
                }
                model_.channels.push_back(name);
                channels_.insert(name);
                continue;
            }
            if (type.is_constant() && type.is_integer()) {
                std::string why;
                std::optional<std::int64_t> value = eval_constant(v.init, why);
                if (!value) {
                    error("TWC013", at, "constant initialiser is not supported: " + why);
                    continue;
                }
                constants_[name] = *value;
                model_.constants.emplace_back(name, *value);
                continue;
            }
            error("TWC014", at,
                  "data variable of type '" + type.str() + "' is not supported",
                  "the aligner gives no semantics to data variables (it ignores their guards and "
                  "updates), so the verified model would not constrain them; see " +
                      std::string(kDocs));
        }
    }

    // ---------------------------------------------------------------- constants
    /// The constant-expression subset evaluated identically by the aligner.
    std::optional<std::int64_t> eval_constant(const UTAP::Expression& e, std::string& why) const {
        if (e.empty()) {
            why = "missing value";
            return std::nullopt;
        }
        switch (e.get_kind()) {
            case Kind::CONSTANT:
                if (!e.get_type().is_integral()) {
                    why = "non-integer literal '" + e.str() + "'";
                    return std::nullopt;
                }
                return static_cast<std::int64_t>(e.get_value());
            case Kind::IDENTIFIER: {
                const auto it = constants_.find(e.get_symbol().get_name());
                if (it == constants_.end()) {
                    why = "'" + e.str() + "' is not an integer constant";
                    return std::nullopt;
                }
                return it->second;
            }
            case Kind::PLUS:
            case Kind::MINUS:
            case Kind::MULT: {
                if (e.get_size() != 2) break;
                std::optional<std::int64_t> a = eval_constant(e[0], why);
                std::optional<std::int64_t> b = eval_constant(e[1], why);
                if (!a || !b) return std::nullopt;
                std::int64_t r = 0;
                const bool overflow =
                    e.get_kind() == Kind::PLUS    ? __builtin_add_overflow(*a, *b, &r)
                    : e.get_kind() == Kind::MINUS ? __builtin_sub_overflow(*a, *b, &r)
                                                  : __builtin_mul_overflow(*a, *b, &r);
                if (overflow) {
                    why = "overflow in '" + e.str() + "'";
                    return std::nullopt;
                }
                return r;
            }
            default:
                break;
        }
        why = "'" + e.str() + "' uses an operator the aligner cannot evaluate "
              "(supported: integer literals, integer constants, +, -, *)";
        return std::nullopt;
    }

    // ---------------------------------------------------------- clock constraints
    std::optional<ir::ClockIndex> clock_of(const UTAP::Expression& e) const {
        if (e.get_kind() != Kind::IDENTIFIER) return std::nullopt;
        const auto it = clock_index_.find(e.get_symbol().get_name());
        if (it == clock_index_.end()) return std::nullopt;
        return it->second;
    }

    static std::optional<ir::Comparison> comparison_of(Kind k) {
        switch (k) {
            case Kind::LT: return ir::Comparison::Less;
            case Kind::LE: return ir::Comparison::LessEqual;
            case Kind::EQ: return ir::Comparison::Equal;
            case Kind::GE: return ir::Comparison::GreaterEqual;
            case Kind::GT: return ir::Comparison::Greater;
            default: return std::nullopt;
        }
    }

    static ir::Comparison mirror(ir::Comparison op) {
        switch (op) {
            case ir::Comparison::Less: return ir::Comparison::Greater;
            case ir::Comparison::LessEqual: return ir::Comparison::GreaterEqual;
            case ir::Comparison::Equal: return ir::Comparison::Equal;
            case ir::Comparison::GreaterEqual: return ir::Comparison::LessEqual;
            case ir::Comparison::Greater: return ir::Comparison::Less;
        }
        return op;
    }

    /**
     * Parse one atom: x ~ c, c ~ x, x - y ~ c, c ~ x - y (diagonals only if
     * @p allow_diagonal: the aligner's guard parser cannot read them).
     */
    bool read_atom(const UTAP::Expression& e, bool allow_diagonal, const std::string& where,
                   std::vector<SourceAtom>& out) {
        const std::optional<ir::Comparison> op = comparison_of(e.get_kind());
        if (!op || e.get_size() != 2) {
            reject_constraint(e, where);
            return false;
        }
        auto try_side = [&](const UTAP::Expression& clock_side, const UTAP::Expression& const_side,
                            ir::Comparison cmp) -> int {
            ir::ClockIndex lhs = 0;
            ir::ClockIndex rhs = ir::kReferenceClock;
            if (std::optional<ir::ClockIndex> c = clock_of(clock_side)) {
                lhs = *c;
            } else if (clock_side.get_kind() == Kind::MINUS && clock_side.get_size() == 2 &&
                       clock_of(clock_side[0]) && clock_of(clock_side[1])) {
                if (!allow_diagonal) {
                    error("TWC020", where,
                          "diagonal clock constraint '" + e.str() + "' in a guard",
                          "the aligner's guard parser silently drops diagonal guards (the verified "
                          "model would have a weaker guard); diagonal constraints are only "
                          "supported in invariants");
                    return -1;
                }
                lhs = *clock_of(clock_side[0]);
                rhs = *clock_of(clock_side[1]);
            } else {
                return 0;  // not this orientation
            }
            std::string why;
            std::optional<std::int64_t> c = eval_constant(const_side, why);
            if (!c) {
                error("TWC021", where, "bound of '" + e.str() + "' is not supported: " + why,
                      "the aligner would silently drop this constraint");
                return -1;
            }
            if (*c > max_constraint_constant() || *c < -max_constraint_constant()) {
                error("TWC022", where,
                      "constant in '" + e.str() + "' is outside the range of UDBM bounds",
                      "the aligner would treat it as infinity");
                return -1;
            }
            std::optional<std::string> named;
            if (const_side.get_kind() == Kind::IDENTIFIER) named = const_side.get_symbol().get_name();
            out.push_back(SourceAtom{ir::ClockConstraint{lhs, rhs, cmp, *c}, e.str(), std::move(named)});
            return 1;
        };
        int r = try_side(e[0], e[1], *op);
        if (r == 0) {
            r = try_side(e[1], e[0], mirror(*op));
        }
        if (r == 0) {
            reject_constraint(e, where);
        }
        return r == 1;
    }

    void reject_constraint(const UTAP::Expression& e, const std::string& where) {
        error("TWC023", where, "unsupported constraint '" + e.str() + "'",
              "only conjunctions (&&) of 'clock ~ c' and, in invariants, 'clock - clock ~ c' are "
              "supported; the aligner would weaken other constraints silently");
    }

    /// Conjunction of atoms; rejects ||, !, ->, constants other than true, etc.
    void read_conjunction(const UTAP::Expression& e, bool allow_diagonal, const std::string& where,
                          std::vector<SourceAtom>& out) {
        if (e.empty()) {
            return;
        }
        switch (e.get_kind()) {
            case Kind::AND:
                for (std::uint32_t i = 0; i < e.get_size(); ++i) {
                    read_conjunction(e[i], allow_diagonal, where, out);
                }
                return;
            case Kind::OR:
                error("TWC024", where, "disjunction '" + e.str() + "' is not supported",
                      "the aligner reads '||' as '&&', which would strengthen the constraint; "
                      "split the edge into one edge per disjunct");
                return;
            case Kind::NOT:
                error("TWC025", where, "negation '" + e.str() + "' is not supported",
                      "the aligner ignores the negation, which would invert the constraint");
                return;
            case Kind::CONSTANT:
                if (e.get_value() != 0) {
                    return;  // 'true': the empty conjunction
                }
                error("TWC026", where, "constant 'false' constraint is not supported",
                      "the aligner treats an unparseable guard as true");
                return;
            default:
                (void)read_atom(e, allow_diagonal, where, out);
                return;
        }
    }

    // ---------------------------------------------------------------- locations
    void read_locations(UTAP::Template& tmpl) {
        std::set<std::string> names;
        for (std::size_t i = 0; i < tmpl.locations.size(); ++i) {
            UTAP::Location& l = tmpl.locations[i];
            const std::string name = l.uid.get_name();
            const std::string where = "location '" + name + "'";
            location_index_[name] = i;
            if (!names.insert(name).second) {
                error("TWC030", where, "duplicate location name");
            }
            const UTAP::Type& t = l.uid.get_type();
            if (t.is(Kind::URGENT) || t.is(Kind::COMMITTED)) {
                error("TWC031", where, "urgent and committed locations are not supported",
                      "the aligner ignores location urgency, which would allow delays the source forbids");
            }
            if (!l.exp_rate.empty() || !l.cost_rate.empty()) {
                error("TWC032", where, "rate expressions (stochastic/priced extensions) are not supported");
            }
            SourceLocation loc{name, {}};
            read_conjunction(l.invariant, /*allow_diagonal=*/true, where + ", invariant", loc.invariant);
            model_.locations.push_back(std::move(loc));
        }
        bool found = false;
        for (std::size_t i = 0; i < tmpl.locations.size(); ++i) {
            if (tmpl.locations[i].uid.get_name() == tmpl.init.get_name()) {
                model_.initial = i;
                found = true;
            }
        }
        if (!found) {
            error("TWC033", "template " + model_.template_name, "the template has no initial location");
        } else if (model_.initial != 0) {
            error("TWC034", "location '" + tmpl.init.get_name() + "'",
                  "the initial location must be the first declared location",
                  "the aligner always starts from the first location in the document; move the "
                  "<init> location to the top so that the verified and the executed initial "
                  "configurations coincide");
        }
    }

    // -------------------------------------------------------------------- edges
    void read_edges(UTAP::Template& tmpl) {
        for (std::size_t i = 0; i < tmpl.edges.size(); ++i) {
            UTAP::Edge& e = tmpl.edges[i];
            std::string where = "edge #" + std::to_string(i);
            if (e.src == nullptr || e.dst == nullptr) {
                error("TWC040", where, "edges must connect two locations (branchpoints are not supported)",
                      "the aligner silently skips such edges");
                continue;
            }
            const std::string src = e.src->uid.get_name();
            const std::string dst = e.dst->uid.get_name();
            where += " (" + src + " -> " + dst + ")";
            if (e.select.get_size() > 0) {
                error("TWC041", where, "select bindings are not supported");
            }
            // UTAP gives every edge the default weight 1 (DocumentBuilder.cpp);
            // only an explicit, different weight makes the edge probabilistic.
            const bool default_weight = e.prob.empty() ||
                                        (e.prob.get_kind() == Kind::CONSTANT && e.prob.get_value() == 1);
            if (!default_weight) {
                error("TWC042", where, "probabilistic edges are not supported");
            }
            SourceEdge edge;
            edge.source = location_index_.at(src);
            edge.target = location_index_.at(dst);
            edge.where = where;
            read_sync(e.sync, where, edge.action);
            read_conjunction(e.guard, /*allow_diagonal=*/false, where + ", guard", edge.guard);
            read_assignments(e.assign, where + ", update", edge.resets);
            model_.edges.push_back(std::move(edge));
        }
    }

    void read_sync(const UTAP::Expression& s, const std::string& where, ir::Action& action) {
        if (s.empty()) {
            action = ir::Action{ir::ActionKind::Internal, ""};
            return;
        }
        if (s.get_kind() != Kind::SYNC || s.get_size() < 1 || s[0].get_kind() != Kind::IDENTIFIER) {
            error("TWC043", where, "unsupported synchronisation '" + s.str() + "'");
            return;
        }
        const std::string channel = s[0].get_symbol().get_name();
        if (channels_.count(channel) == 0) {
            error("TWC044", where, "synchronisation on '" + channel + "', which is not a declared plain channel");
            return;
        }
        switch (s.get_sync()) {
            case UTAP::Constants::SYNC_BANG:
                action = ir::Action{ir::ActionKind::Send, channel};
                return;
            case UTAP::Constants::SYNC_QUE:
                action = ir::Action{ir::ActionKind::Receive, channel};
                return;
            default:
                error("TWC045", where, "unsupported synchronisation kind in '" + s.str() + "'");
                return;
        }
    }

    void read_assignments(const UTAP::Expression& a, const std::string& where,
                          std::vector<ir::ClockIndex>& resets) {
        if (a.empty()) {
            return;
        }
        if (a.get_kind() == Kind::COMMA) {
            for (std::uint32_t i = 0; i < a.get_size(); ++i) {
                read_assignments(a[i], where, resets);
            }
            return;
        }
        if (a.get_kind() == Kind::CONSTANT && a.get_value() == 1) {
            return;  // UTAP's representation of an empty update in some versions
        }
        if (a.get_kind() != Kind::ASSIGN || a.get_size() != 2) {
            error("TWC050", where, "unsupported update '" + a.str() + "'",
                  "only clock resets 'x := 0' are supported (the aligner ignores other updates)");
            return;
        }
        std::optional<ir::ClockIndex> clock = clock_of(a[0]);
        if (!clock) {
            error("TWC051", where, "update of '" + a[0].str() + "', which is not a clock",
                  "data variables are not part of the aligner's semantics");
            return;
        }
        std::string why;
        std::optional<std::int64_t> value = eval_constant(a[1], why);
        if (!value || *value != 0) {
            error("TWC052", where, "clock '" + a[0].str() + "' must be reset to 0, found '" + a[1].str() + "'",
                  "the aligner silently ignores non-zero clock resets");
            return;
        }
        resets.push_back(*clock);
    }

    UTAP::Document& doc_;
    const ReaderOptions& options_;
    std::vector<Diagnostic>& diags_;
    bool system_declaration_ignored_{false};
    std::size_t error_count_{0};
    SourceModel model_;
    std::map<std::string, ir::ClockIndex> clock_index_;
    std::map<std::string, std::int64_t> constants_;
    std::set<std::string> channels_;
    std::map<std::string, std::size_t> location_index_;
};

}  // namespace

std::int64_t max_constraint_constant() noexcept {
    // UDBM encodes bounds as (c << 1 | strictness) in int32 and reserves
    // dbm_INFINITY = INT_MAX >> 1; keep a safety margin of one.
    return static_cast<std::int64_t>(dbm_INFINITY) - 1;
}

std::optional<SourceModel> read_strict(UTAP::Document& doc, const ReaderOptions& options,
                                       std::vector<Diagnostic>& diagnostics) {
    return StrictReader(doc, options, diagnostics).run();
}

}  // namespace twin::compiler::detail

namespace twin::compiler {

ReadResult read_uppaal(std::string_view xml, const ReaderOptions& options) {
    const std::lock_guard<std::recursive_mutex> utap_lock(utap_mutex());  // UTAP is not thread-safe
    ReadResult out;
    UTAP::Document doc;
    const std::string buffer(xml);
    if (parse_XML_buffer(buffer.c_str(), doc, true) != 0 && doc.get_errors().empty()) {
        out.diagnostics.push_back(Diagnostic{Severity::Error, "TWC000", "UTAP cannot parse the document", "document",
                                             "check that the file is a UPPAAL XML (flat) document"});
        return out;
    }
    out.model = detail::read_strict(doc, options, out.diagnostics);
    return out;
}

}  // namespace twin::compiler

namespace twin::compiler::detail {

const std::set<std::string>& utap_builtin_names() {
    // Parse a minimal document with the same UTAP and record its globals: robust
    // against changes of UTAP's built-in prelude across versions.
    static const std::set<std::string> names = [] {
        std::set<std::string> out;
        UTAP::Document doc;
        const char* xml =
            "<nta><declaration></declaration><template><name>T</name>"
            "<location id=\"a\"><name>a</name></location><init ref=\"a\"/></template>"
            "<system>P = T(); system P;</system></nta>";
        if (parse_XML_buffer(xml, doc, true) == 0) {
            for (const UTAP::Variable& v : doc.get_globals().variables) {
                out.insert(v.uid.get_name());
            }
        }
        return out;
    }();
    return names;
}

}  // namespace twin::compiler::detail
