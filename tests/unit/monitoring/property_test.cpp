/**
 * @file property_test.cpp
 * @brief Property language: parsing, printing, and evaluation on the kernel's committed state sets.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <variant>

#include "twin/compiler/compiler.hpp"
#include "twin/kernel/model.hpp"
#include "twin/kernel/state_set.hpp"
#include "twin/monitoring/property.hpp"

namespace twin::monitoring {
namespace {

using Kind = StateFormula::Kind;

Property parse_ok(const std::string& text) {
    Result<Property> p = parse_property(text);
    EXPECT_TRUE(p) << text << ": " << (p ? "" : p.error().to_string());
    return p ? p.value() : Property{};
}

TEST(PropertyParse, PrecedenceImpliesOrAndNot) {
    const Property p = parse_ok("A[] !FAULT || NORMAL && t <= 5 -> STOPPED");
    EXPECT_EQ(p.quantifier, Quantifier::Always);
    ASSERT_EQ(p.phi.kind, Kind::Implies);
    const StateFormula& lhs = p.phi.children.at(0);
    ASSERT_EQ(lhs.kind, Kind::Or);
    EXPECT_EQ(lhs.children.at(0).kind, Kind::Not);
    EXPECT_EQ(lhs.children.at(0).children.at(0).name, "FAULT");
    ASSERT_EQ(lhs.children.at(1).kind, Kind::And);
    const StateFormula& clock = lhs.children.at(1).children.at(1);
    EXPECT_EQ(clock.kind, Kind::Clock);
    EXPECT_EQ(clock.name, "t");
    EXPECT_EQ(clock.op, ir::Comparison::LessEqual);
    EXPECT_EQ(clock.bound, 5);
    EXPECT_EQ(p.phi.children.at(1).name, "STOPPED");
}

TEST(PropertyParse, QuantifiersSemanticAtomsDiagonalsAndProcessPrefixes) {
    EXPECT_EQ(parse_ok("E<> COOLING").quantifier, Quantifier::Eventually);
    EXPECT_EQ(parse_ok("A<> STOPPED").quantifier, Quantifier::Inevitably);
    EXPECT_EQ(parse_ok("E[] NORMAL").quantifier, Quantifier::Potentially);
    const Property leads = parse_ok("DEGRADED --> NORMAL || STOPPING");
    EXPECT_EQ(leads.quantifier, Quantifier::LeadsTo);
    ASSERT_TRUE(leads.psi.has_value());
    EXPECT_EQ(leads.psi->kind, Kind::Or);

    const Property sem = parse_ok("A[] sem((not (> bearing_temp bearing_temp_limit)))");
    ASSERT_EQ(sem.phi.kind, Kind::Semantic);
    EXPECT_EQ(sem.phi.formula, "(not (> bearing_temp bearing_temp_limit))");

    const Property diag = parse_ok("A[] t - u <= 5");
    ASSERT_EQ(diag.phi.kind, Kind::Clock);
    EXPECT_EQ(diag.phi.minus, std::optional<std::string>("u"));

    EXPECT_EQ(parse_ok("A[] Pump.FAULT").phi.name, "FAULT");
    EXPECT_EQ(parse_ok("A[] true").phi.kind, Kind::True);
    EXPECT_EQ(parse_ok("A[] 3 > t").phi.op, ir::Comparison::Less);  // mirrored to t < 3
}

TEST(PropertyParse, PrintParseRoundTrip) {
    for (const char* text : {"A[] !FAULT || NORMAL && t <= 5 -> STOPPED", "E<> COOLING", "DEGRADED --> NORMAL",
                             "A[] sem((and a (or b c))) && !FAULT", "A[] t - u < 2"}) {
        const Property p = parse_ok(text);
        const Property again = parse_ok(print_property(p));
        EXPECT_EQ(print_property(again), print_property(p)) << text;
        EXPECT_EQ(again, p) << text;
    }
}

TEST(PropertyParse, ErrorsCarryAColumn) {
    for (const char* bad : {"A[]", "A[] (NORMAL", "A[] t <= x", "A[] sem((a b)", "B[] NORMAL", "A[] t < 1.5",
                            "A[] NORMAL NORMAL", "A[] t <="}) {
        Result<Property> p = parse_property(bad);
        ASSERT_FALSE(p) << bad;
        EXPECT_EQ(p.error().code, ErrorCode::ParseError) << bad;
        EXPECT_FALSE(p.error().context_value("column").empty()) << bad;
    }
}

TEST(PropertyQueries, CollectNamesAndKinds) {
    const Property p = parse_ok("A[] (NORMAL && t <= 5) -> sem(ok)");
    const Atoms a = atoms(p.phi);
    EXPECT_EQ(a.locations, (std::set<std::string>{"NORMAL"}));
    EXPECT_EQ(a.clocks, (std::set<std::string>{"t"}));
    EXPECT_EQ(a.semantic, (std::vector<std::string>{"ok"}));
}

class PumpStates : public ::testing::Test {
protected:
    void SetUp() override {
        compiler::CompileOptions o;
        o.model_id = "pump";
        auto out = compiler::compile_file(std::filesystem::path(TWIN_SOURCE_DIR) / "examples/industrial-pump/models/pump_dt.xml", o);
        ASSERT_TRUE(std::holds_alternative<compiler::CompileResult>(out));
        Result<std::shared_ptr<const kernel::Model>> m = kernel::Model::create(std::get<compiler::CompileResult>(out).model);
        ASSERT_TRUE(m);
        model_ = m.value();
    }
    kernel::Configuration at(const std::string& location, double t_seconds) const {
        kernel::Configuration c;
        c.location = *ir::find_location(model_->ir(), location);
        c.clocks = {static_cast<Ticks>(t_seconds * 1000)};
        c.time = c.clocks[0];
        return c;
    }
    std::shared_ptr<const kernel::Model> model_;
};

TEST_F(PumpStates, VerdictsOnStateSets) {
    const StateFormula not_fault = parse_ok("A[] !FAULT").phi;
    Result<Verdict> ok = evaluate(not_fault, *model_, kernel::StateSet::of(at("NORMAL", 3)));
    ASSERT_TRUE(ok) << ok.error().to_string();
    EXPECT_EQ(ok.value(), Verdict::Satisfied);
    EXPECT_EQ(evaluate(not_fault, *model_, kernel::StateSet::of(at("FAULT", 3))).value(), Verdict::Violated);
    Result<kernel::StateSet> both = kernel::StateSet::of({at("NORMAL", 3), at("FAULT", 3)});
    ASSERT_TRUE(both);
    EXPECT_EQ(evaluate(not_fault, *model_, both.value()).value(), Verdict::Inconclusive);

    const StateFormula bounded = parse_ok("A[] DEGRADED -> t <= 100").phi;
    EXPECT_EQ(evaluate(bounded, *model_, kernel::StateSet::of(at("DEGRADED", 99.999))).value(), Verdict::Satisfied);
    EXPECT_EQ(evaluate(bounded, *model_, kernel::StateSet::of(at("DEGRADED", 100.001))).value(), Verdict::Violated);
    EXPECT_EQ(evaluate(bounded, *model_, kernel::StateSet::of(at("NORMAL", 500))).value(), Verdict::Satisfied);
}

TEST_F(PumpStates, UnknownNamesAndSemanticAtomsAreRefused) {
    EXPECT_FALSE(evaluate(parse_ok("A[] NOWHERE").phi, *model_, kernel::StateSet::of(at("NORMAL", 1))));
    EXPECT_FALSE(evaluate(parse_ok("A[] zz < 3").phi, *model_, kernel::StateSet::of(at("NORMAL", 1))));
    EXPECT_FALSE(evaluate(parse_ok("A[] sem(ok)").phi, *model_, kernel::StateSet::of(at("NORMAL", 1))));
}

}  // namespace
}  // namespace twin::monitoring
