// The expression language: parsing, printing, checking, translation to SMT-LIB2 and back.
#include <gtest/gtest.h>

#include <map>

#include "twin/model/expression.hpp"

namespace twin::model::expr {
namespace {

Resolver table() {
    static const std::map<std::string, Symbol, std::less<>> symbols = {
        {"temperature", {Symbol::Type::Number, "probe__temperature", {}}},
        {"limit", {Symbol::Type::Number, "limit", {}}},
        {"on", {Symbol::Type::Boolean, "heater__on", {}}},
        {"door.open", {Symbol::Type::Boolean, "door__open", {}}},
        {"mode", {Symbol::Type::Enum, "mode", {"OFF", "AUTO", "MANUAL"}}},
        {"serial", {Symbol::Type::String, "serial", {}}},
    };
    return [](std::string_view p) -> std::optional<Symbol> {
        const auto it = symbols.find(p);
        if (it == symbols.end()) return std::nullopt;
        return it->second;
    };
}

std::string smt(std::string_view text) {
    const Translated t = condition(text, table());
    EXPECT_FALSE(t.error.has_value()) << text << ": " << (t.error ? t.error->message : "");
    return t.smt;
}

std::string error_code(std::string_view text) {
    const Translated t = condition(text, table());
    return t.error ? t.error->code : std::string("none");
}

TEST(ModelExpression, TranslatesToQfLra) {
    EXPECT_EQ(smt("temperature > limit"), "(> probe__temperature limit)");
    EXPECT_EQ(smt("on and not door.open and temperature <= limit"), "(and heater__on (not door__open) (<= probe__temperature limit))");
    EXPECT_EQ(smt("temperature >= -40"), "(>= probe__temperature (- 40))");
    EXPECT_EQ(smt("temperature < limit - 5"), "(< probe__temperature (- limit 5))");
    EXPECT_EQ(smt("temperature > 1.5 * limit"), "(> probe__temperature (* 1.5 limit))");
    EXPECT_EQ(smt("temperature != limit"), "(not (= probe__temperature limit))");
    EXPECT_EQ(smt("mode == AUTO"), "(= mode 1)");
    EXPECT_EQ(smt("mode != MANUAL or on"), "(or (not (= mode 2)) heater__on)");
    EXPECT_EQ(smt("(on or door.open) and true"), "(and (or heater__on door__open) true)");
}

TEST(ModelExpression, ReportsErrorsWithCodes) {
    EXPECT_EQ(error_code("temperature >"), "TML020");
    EXPECT_EQ(error_code("temperature"), "TML021");        // a number is not a condition
    EXPECT_EQ(error_code("on > 3"), "TML021");
    EXPECT_EQ(error_code("pressure > 3"), "TML022");       // unknown name
    EXPECT_EQ(error_code("temperature * limit > 3"), "TML023");  // non-linear
    EXPECT_EQ(error_code("serial == 3"), "TML011");        // text property
    EXPECT_EQ(error_code("mode == HEATING"), "TML021");    // not one of the literals
    EXPECT_EQ(error_code("mode > 1"), "TML021");
}

TEST(ModelExpression, PrintParsesBackToTheSameTree) {
    for (const char* text : {"a and b or c", "a and (b or c)", "not (a and b)", "not a == b", "a - (b - c) > 0", "-(a + b) < 3",
                             "a - b - c >= 2", "x * 2 + 1 == y", "(a or b) and (c or d)"}) {
        const Parsed p = parse(text);
        ASSERT_TRUE(p.node.has_value()) << text << ": " << p.error;
        const Parsed again = parse(print(*p.node));
        ASSERT_TRUE(again.node.has_value()) << print(*p.node);
        EXPECT_TRUE(p.node->same(*again.node)) << text << " printed as " << print(*p.node);
    }
}

TEST(ModelExpression, SmtRoundTripsThroughTheGrammar) {
    const Resolver identity = [](std::string_view p) -> std::optional<Symbol> {
        if (p == "heater_on" || p == "door_open" || p == "reset_requested") return Symbol{Symbol::Type::Boolean, std::string(p), {}};
        return Symbol{Symbol::Type::Number, std::string(p), {}};
    };
    for (const char* f : {"(and (not heater_on) (<= temperature temperature_limit))", "(> temperature temperature_limit)",
                          "(and reset_requested (<= temperature temperature_limit))", "(or (> bearing_temp limit) (> vibration_rms vlimit))",
                          "(>= temperature (- 40))", "(= limit 80)", "(not (= a b))", "(< x (- y 5))", "(> c (* 1.5 r))"}) {
        const auto n = from_smt(f);
        ASSERT_TRUE(n.has_value()) << f;
        const Translated t = to_smt(*n, identity);
        ASSERT_FALSE(t.error.has_value()) << f << ": " << t.error->message;
        EXPECT_EQ(t.smt, normalize_smt(f)) << "expression: " << print(*n);
    }
    EXPECT_FALSE(from_smt("(=> a b)").has_value());
    EXPECT_FALSE(from_smt("(forall ((x Real)) (> x 0))").has_value());
    EXPECT_FALSE(from_smt("f(x)").has_value());
}

TEST(ModelExpression, RawSmtPassesThrough) {
    EXPECT_EQ(smt("smt: (=> a  b)"), "(=> a b)");
}

TEST(ModelExpression, ConjunctsSplitTopLevelArguments) {
    EXPECT_EQ(conjuncts("(and a (or b c) (not d))"), (std::vector<std::string>{"a", "(or b c)", "(not d)"}));
    EXPECT_EQ(conjuncts("(or a b)"), (std::vector<std::string>{"(or a b)"}));
    EXPECT_EQ(conjuncts("x"), (std::vector<std::string>{"x"}));
}

}  // namespace
}  // namespace twin::model::expr
