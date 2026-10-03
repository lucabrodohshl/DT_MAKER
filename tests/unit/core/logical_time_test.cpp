/**
 * @file logical_time_test.cpp
 * @brief Exact parsing/formatting of logical time on the decimal grid.
 */
#include <gtest/gtest.h>

#include "twin/core/logical_time.hpp"

namespace twin {
namespace {

constexpr TimeBase kMillis{1000};

TEST(LogicalTime, ParsesDecimalsExactly) {
    EXPECT_EQ(parse_time("31.5", kMillis).value(), 31500);
    EXPECT_EQ(parse_time("0.001", kMillis).value(), 1);
    EXPECT_EQ(parse_time("12", kMillis).value(), 12000);
    EXPECT_EQ(parse_time("0", kMillis).value(), 0);
    EXPECT_EQ(parse_time("7.250000", kMillis).value(), 7250);  // trailing zeros are harmless
    EXPECT_EQ(parse_time("0.1", TimeBase{10}).value(), 1);
}

TEST(LogicalTime, RejectsOffGridTimestampsInsteadOfRounding) {
    Result<Ticks> r = parse_time("0.0001", kMillis);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::TimeNotRepresentable);
    EXPECT_EQ(parse_time("1.5", TimeBase{1}).error().code, ErrorCode::TimeNotRepresentable);
}

TEST(LogicalTime, RejectsMalformedSyntax) {
    for (const char* bad : {"", "-1", "+1", "1e3", " 1", "1.", ".5", "1.2.3", "abc", "1,5"}) {
        Result<Ticks> r = parse_time(bad, kMillis);
        ASSERT_FALSE(r.ok()) << bad;
        EXPECT_EQ(r.error().code, ErrorCode::ParseError) << bad;
    }
}

TEST(LogicalTime, RejectsValuesBeyondTheHorizon) {
    Result<Ticks> r = parse_time("99999999999999999999", kMillis);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::ArithmeticOverflow);
}

TEST(LogicalTime, FormatIsExactAndMinimal) {
    EXPECT_EQ(format_time(31500, kMillis), "31.5");
    EXPECT_EQ(format_time(1, kMillis), "0.001");
    EXPECT_EQ(format_time(12000, kMillis), "12");
    EXPECT_EQ(format_time(0, kMillis), "0");
    EXPECT_EQ(format_time(-2500, kMillis), "-2.5");
}

TEST(LogicalTime, FormatParseRoundTrip) {
    for (Ticks t : {Ticks{0}, Ticks{1}, Ticks{999}, Ticks{1000}, Ticks{123456789}, kMaxTicks}) {
        EXPECT_EQ(parse_time(format_time(t, kMillis), kMillis).value(), t);
    }
}

TEST(LogicalTime, TimeBaseMustBeAPowerOfTen) {
    EXPECT_TRUE(validate_time_base(TimeBase{1}).ok());
    EXPECT_TRUE(validate_time_base(TimeBase{1000}).ok());
    EXPECT_TRUE(validate_time_base(TimeBase{1'000'000'000}).ok());
    EXPECT_FALSE(validate_time_base(TimeBase{0}).ok());
    EXPECT_FALSE(validate_time_base(TimeBase{3}).ok());
    EXPECT_FALSE(validate_time_base(TimeBase{10'000'000'000}).ok());
}

TEST(LogicalTime, ConstantScalingIsCheckedNotWrapped) {
    EXPECT_EQ(units_to_ticks(30, kMillis).value(), 30000);
    EXPECT_EQ(units_to_ticks(-5, kMillis).value(), -5000);
    EXPECT_EQ(units_to_ticks(kMaxModelConstant + 1, kMillis).error().code,
              ErrorCode::ArithmeticOverflow);
}

TEST(LogicalTime, CheckedAddDetectsOverflow) {
    EXPECT_EQ(checked_add(1, 2).value(), 3);
    EXPECT_EQ(checked_add(INT64_MAX, 1).error().code, ErrorCode::ArithmeticOverflow);
}

}  // namespace
}  // namespace twin
