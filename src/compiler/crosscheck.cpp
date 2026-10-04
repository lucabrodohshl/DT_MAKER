/**
 * @file crosscheck.cpp
 * @brief Translation validation against dtpta::TimedAutomaton, using UDBM zones.
 */
#include "crosscheck.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <sstream>

#include "dbm/constraints.h"
#include "dbm/dbm.h"
#include "dtpta/timedautomaton.h"

namespace twin::compiler::detail {
namespace {

using Zone = std::vector<raw_t>;

/// DBM encoding of  x_i - x_j ~ c  (identical to the aligner's add_dbm_constraint).
std::vector<constraint_t> to_dbm(const ir::ClockConstraint& a) {
    const auto i = static_cast<cindex_t>(a.lhs);
    const auto j = static_cast<cindex_t>(a.rhs);
    const auto c = static_cast<int32_t>(a.bound);
    switch (a.op) {
        case ir::Comparison::Less: return {{i, j, dbm_bound2raw(c, dbm_STRICT)}};
        case ir::Comparison::LessEqual: return {{i, j, dbm_bound2raw(c, dbm_WEAK)}};
        case ir::Comparison::Equal:
            return {{i, j, dbm_bound2raw(c, dbm_WEAK)}, {j, i, dbm_bound2raw(-c, dbm_WEAK)}};
        case ir::Comparison::GreaterEqual: return {{j, i, dbm_bound2raw(-c, dbm_WEAK)}};
        case ir::Comparison::Greater: return {{j, i, dbm_bound2raw(-c, dbm_STRICT)}};
    }
    return {};
}

/// All non-negative valuations.
Zone universe(cindex_t dim) {
    Zone z(static_cast<std::size_t>(dim) * dim);
    dbm_init(z.data(), dim);
    return z;
}

/// Intersect with constraints; returns an empty vector for the empty zone.
Zone constrain(Zone z, cindex_t dim, const std::vector<constraint_t>& cs) {
    for (const constraint_t& c : cs) {
        if (!dbm_constrain1(z.data(), dim, c.i, c.j, c.value)) {
            return {};
        }
    }
    if (!dbm_close(z.data(), dim) || dbm_isEmpty(z.data(), dim)) {
        return {};
    }
    return z;
}

std::vector<constraint_t> encode(const std::vector<SourceAtom>& atoms) {
    std::vector<constraint_t> out;
    for (const SourceAtom& a : atoms) {
        for (const constraint_t& c : to_dbm(a.constraint)) {
            out.push_back(c);
        }
    }
    return out;
}

bool same_zone(const Zone& a, const Zone& b, cindex_t dim) {
    if (a.empty() || b.empty()) {
        return a.empty() && b.empty();
    }
    return dbm_areEqual(a.data(), b.data(), dim);
}

/// Location names as recorded by the aligner (from its public text dump).
std::map<int, std::string> aligner_location_names(const dtpta::TimedAutomaton& ta) {
    std::map<int, std::string> names;
    std::mt19937_64 rng(std::random_device{}());
    const std::filesystem::path tmp = std::filesystem::temp_directory_path() /
                                      ("twin-crosscheck-" + std::to_string(rng()) + ".txt");
    ta.save_as_simple_file(tmp.string());
    std::ifstream in(tmp);
    std::string line;
    bool in_locations = false;
    const std::regex entry(R"(^  (\d+): (.+)$)");
    while (std::getline(in, line)) {
        if (line == "Locations:") {
            in_locations = true;
            continue;
        }
        if (line.empty() || line == "Transitions:") {
            in_locations = false;
            continue;
        }
        std::smatch m;
        if (in_locations && std::regex_match(line, m, entry)) {
            names[std::stoi(m[1].str())] = m[2].str();
        }
    }
    in.close();
    std::error_code ignored;
    std::filesystem::remove(tmp, ignored);
    return names;
}

class Checker {
public:
    explicit Checker(TranslationValidation& tv) : tv_(tv) {}
    template <class A, class B>
    void equal(const A& ours, const B& theirs, const std::string& what) {
        ++tv_.checks;
        if (!(ours == theirs)) {
            std::ostringstream os;
            os << what << ": compiler reads '" << ours << "', aligner reads '" << theirs << "'";
            tv_.mismatches.push_back(os.str());
        }
    }
    void zones(const Zone& ours, const Zone& theirs, cindex_t dim, const std::string& what) {
        ++tv_.checks;
        if (!same_zone(ours, theirs, dim)) {
            tv_.mismatches.push_back(what + ": compiler and aligner constraints denote different zones");
        }
    }

private:
    TranslationValidation& tv_;
};

}  // namespace

TranslationValidation crosscheck_with_aligner(const std::filesystem::path& source,
                                              const SourceModel& model) {
    TranslationValidation tv;
    std::unique_ptr<dtpta::TimedAutomaton> ta;
    try {
        ta = std::make_unique<dtpta::TimedAutomaton>(source.string());
    } catch (const std::exception& e) {
        tv.mismatches.push_back(std::string("the aligner cannot read the model: ") + e.what());
        return tv;
    }
    Checker check(tv);
    const auto dim = static_cast<cindex_t>(model.clocks.size() + 1);

    check.equal(model.template_name, ta->get_name(), "template name");
    check.equal(dim, ta->get_dimension(), "DBM dimension (clocks + 1)");
    const auto& clock_map = ta->get_clock_map();
    check.equal(model.clocks.size(), clock_map.size(), "number of clocks");
    for (std::size_t k = 0; k < model.clocks.size(); ++k) {
        const auto it = clock_map.find(model.clocks[k]);
        check.equal(k + 1, it == clock_map.end() ? std::size_t{0} : static_cast<std::size_t>(it->second),
                    "index of clock '" + model.clocks[k] + "'");
    }

    // Locations: names/order, and invariants compared as zones.
    check.equal(model.locations.size(), static_cast<std::size_t>(ta->get_num_locations()),
                "number of locations");
    const std::map<int, std::string> names = aligner_location_names(*ta);
    for (std::size_t i = 0; i < model.locations.size(); ++i) {
        const auto it = names.find(static_cast<int>(i));
        check.equal(model.locations[i].name, it == names.end() ? std::string("<missing>") : it->second,
                    "name of location #" + std::to_string(i));
        const Zone ours = constrain(universe(dim), dim, encode(model.locations[i].invariant));
        const Zone theirs = ta->apply_invariants(universe(dim), static_cast<int>(i));
        check.zones(ours, theirs, dim, "invariant of location '" + model.locations[i].name + "'");
    }
    check.equal(std::size_t{0}, model.initial, "initial location index (the aligner starts at #0)");

    // Transitions: order, endpoints, action, guard zone, resets.
    const std::vector<dtpta::Transition>& theirs = ta->get_transitions();
    check.equal(model.edges.size(), theirs.size(), "number of transitions");
    const std::size_t n = std::min(model.edges.size(), theirs.size());
    for (std::size_t k = 0; k < n; ++k) {
        const SourceEdge& e = model.edges[k];
        const dtpta::Transition& t = theirs[k];
        const std::string what = "transition #" + std::to_string(k) + " " + e.where;
        check.equal(e.source, static_cast<std::size_t>(t.from_location), what + " source");
        check.equal(e.target, static_cast<std::size_t>(t.to_location), what + " target");
        const std::string our_label = e.action.label();
        const std::string their_label =
            t.has_synchronization() ? t.channel + (t.is_sender ? "!" : "?") : t.action;
        check.equal(our_label, their_label, what + " action");
        Zone their_guard = universe(dim);
        bool consistent = true;
        for (const constraint_t& c : t.guards) {
            consistent = consistent && dbm_constrain1(their_guard.data(), dim, c.i, c.j, c.value);
        }
        if (!consistent || !dbm_close(their_guard.data(), dim) || dbm_isEmpty(their_guard.data(), dim)) {
            their_guard.clear();
        }
        check.zones(constrain(universe(dim), dim, encode(e.guard)), their_guard, dim, what + " guard");
        std::set<std::size_t> our_resets(e.resets.begin(), e.resets.end());
        std::set<std::size_t> their_resets;
        for (cindex_t r : t.resets) {
            their_resets.insert(static_cast<std::size_t>(r));
        }
        ++tv.checks;
        if (our_resets != their_resets) {
            tv.mismatches.push_back(what + " resets differ");
        }
    }
    tv.passed = tv.mismatches.empty();
    return tv;
}

DeterminismReport analyse_determinism(const SourceModel& model,
                                      const std::vector<std::string>& transition_ids) {
    DeterminismReport report;
    const auto dim = static_cast<cindex_t>(model.clocks.size() + 1);
    for (std::size_t a = 0; a < model.edges.size(); ++a) {
        for (std::size_t b = a + 1; b < model.edges.size(); ++b) {
            const SourceEdge& ea = model.edges[a];
            const SourceEdge& eb = model.edges[b];
            if (ea.source != eb.source || ea.action.kind == ir::ActionKind::Internal ||
                !(ea.action == eb.action)) {
                continue;
            }
            std::vector<constraint_t> cs = encode(model.locations[ea.source].invariant);
            for (const constraint_t& c : encode(ea.guard)) cs.push_back(c);
            for (const constraint_t& c : encode(eb.guard)) cs.push_back(c);
            if (!constrain(universe(dim), dim, cs).empty()) {
                report.overlapping.emplace_back(transition_ids.at(a), transition_ids.at(b));
            }
        }
    }
    report.event_deterministic = report.overlapping.empty();
    return report;
}

}  // namespace twin::compiler::detail
