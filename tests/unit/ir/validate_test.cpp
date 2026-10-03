/**
 * @file validate_test.cpp
 * @brief IR well-formedness rules.
 */
#include <gtest/gtest.h>

#include "support/model_builder.hpp"
#include "twin/ir/validate.hpp"

namespace twin::ir {
namespace {

using test::ModelBuilder;
using B = ModelBuilder;

Model base() {
    return ModelBuilder("m")
        .clock("x")
        .location("A")
        .location("B", {B::c("x", "<=", 5)})
        .transition("A", "go!", "B", {B::c("x", ">=", 1)}, {"x"})
        .build();
}

TEST(IrValidate, AcceptsBuilderModels) { EXPECT_TRUE(validate(base()).ok()); }

TEST(IrValidate, RejectsDuplicateLocations) {
    Model m = base();
    m.locations[1].id = "A";
    m.propositions[1].id = "at(A)";
    EXPECT_FALSE(validate(m).ok());
}

TEST(IrValidate, RejectsNonCanonicalConjunctions) {
    Model m = base();
    m.locations[1].invariant.push_back(m.locations[1].invariant[0]);  // duplicate atom
    EXPECT_FALSE(validate(m).ok());
}

TEST(IrValidate, RejectsUnknownClocksAndSelfComparison) {
    Model m = base();
    m.transitions[0].guard[0].lhs = 7;
    EXPECT_FALSE(validate(m).ok());
    Model m2 = base();
    m2.transitions[0].guard[0].rhs = m2.transitions[0].guard[0].lhs;
    EXPECT_FALSE(validate(m2).ok());
}

TEST(IrValidate, RejectsUndeclaredChannels) {
    Model m = base();
    m.channels.clear();
    EXPECT_FALSE(validate(m).ok());
}

TEST(IrValidate, RejectsBadResetsAndInitial) {
    Model m = base();
    m.transitions[0].resets = {1, 1};
    EXPECT_FALSE(validate(m).ok());
    Model m2 = base();
    m2.initial = 9;
    EXPECT_FALSE(validate(m2).ok());
}

TEST(IrValidate, RequiresOnePropositionPerLocation) {
    Model m = base();
    m.propositions.pop_back();
    EXPECT_FALSE(validate(m).ok());
    Model m2 = base();
    m2.propositions[0].id = "at(B)";
    EXPECT_FALSE(validate(m2).ok());
}

TEST(IrValidate, RejectsInterpretationsOfAbsentLabels) {
    Model m = base();
    m.event_interpretations.push_back(EventInterpretation{"nope!", "true"});
    EXPECT_FALSE(validate(m).ok());
}

TEST(IrValidate, ReportsAllViolations) {
    Model m = base();
    m.initial = 9;
    m.channels.clear();
    EXPECT_GE(validate_all(m).size(), 2U);
}

}  // namespace
}  // namespace twin::ir
