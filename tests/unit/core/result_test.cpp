/**
 * @file result_test.cpp
 * @brief Result<T> / Status / Error behaviour.
 */
#include <gtest/gtest.h>

#include "twin/core/result.hpp"

namespace twin {
namespace {

Result<int> half(int x) {
    if (x % 2 != 0) {
        return make_error(ErrorCode::InvalidArgument, "odd").with("x", std::to_string(x));
    }
    return x / 2;
}

TEST(Result, HoldsValueOrError) {
    Result<int> ok = half(4);
    ASSERT_TRUE(ok.ok());
    EXPECT_EQ(ok.value(), 2);
    Result<int> bad = half(3);
    ASSERT_FALSE(bad.ok());
    EXPECT_EQ(bad.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(bad.error().context_value("x"), "3");
}

TEST(Result, WrongAccessIsAProgrammingErrorNotUB) {
    EXPECT_THROW((void)half(3).value(), BadResultAccess);
    EXPECT_THROW((void)half(4).error(), BadResultAccess);
    Status s;
    EXPECT_THROW((void)s.error(), BadResultAccess);
}

TEST(Error, RendersCodeMessageAndContext) {
    Error e = make_error(ErrorCode::InvariantViolation, "too late").with("clock", "t_mode");
    EXPECT_EQ(e.to_string(), "invariant_violation: too late [clock=t_mode]");
    EXPECT_EQ(to_string(ErrorCode::IncompatibleObservation), "incompatible_observation");
}

}  // namespace
}  // namespace twin
