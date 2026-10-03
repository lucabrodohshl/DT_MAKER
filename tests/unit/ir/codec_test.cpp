/**
 * @file codec_test.cpp
 * @brief Canonical IR serialisation: determinism, round trip, strict decoding.
 */
#include <gtest/gtest.h>

#include "support/model_builder.hpp"
#include "twin/ir/codec.hpp"

namespace twin::ir {
namespace {

using test::ModelBuilder;
using B = ModelBuilder;

Model sample() {
    return ModelBuilder("sample")
        .clock("x")
        .clock("y")
        .location("Idle")
        .location("Busy", {B::c("x", "<=", 10), B::diff("x", "y", "<", 3)})
        .transition("Idle", "start!", "Busy", {B::c("y", ">=", 2)}, {"x"})
        .transition("Busy", "done!", "Idle", {B::c("x", ">=", 4)}, {"x", "y"})
        .transition("Busy", "tau", "Busy", {}, {})
        .interpret_location("Busy", "(> load 0)")
        .build();
}

TEST(IrCodec, CanonicalTextIsDeterministic) {
    const Model m = sample();
    Result<std::string> a = to_canonical_text(m);
    Result<std::string> b = to_canonical_text(sample());
    ASSERT_TRUE(a.ok()) << a.error().to_string();
    EXPECT_EQ(a.value(), b.value());
    EXPECT_EQ(ir_sha256(m).value(), ir_sha256(sample()).value());
    EXPECT_EQ(a.value().find('\n'), std::string::npos);
    EXPECT_EQ(a.value().find(": "), std::string::npos);
}

TEST(IrCodec, RoundTripsExactly) {
    const Model m = sample();
    const std::string text = to_canonical_text(m).value();
    Result<Model> back = from_canonical_text(text);
    ASSERT_TRUE(back.ok()) << back.error().to_string();
    EXPECT_EQ(back.value(), m);
}

TEST(IrCodec, RejectsNonCanonicalText) {
    const std::string text = to_canonical_text(sample()).value();
    json::Json doc = json::parse(text).value();
    const std::string pretty = doc.dump(2);
    Result<Model> r = from_canonical_text(pretty);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::IntegrityError);
}

TEST(IrCodec, RejectsDuplicateKeys) {
    std::string text = to_canonical_text(sample()).value();
    // Duplicate the "initial" member: parsers silently keep one; we must refuse.
    const std::string dup = R"("initial":"Busy","initial":"Idle")";
    const auto pos = text.find(R"("initial":"Idle")");
    ASSERT_NE(pos, std::string::npos);
    text.replace(pos, std::string(R"("initial":"Idle")").size(), dup);
    EXPECT_FALSE(from_canonical_text(text).ok());
}

TEST(IrCodec, RejectsUnknownMembersAndWrongFormat) {
    json::Json doc = to_json(sample());
    doc["extra"] = 1;
    EXPECT_FALSE(from_json(doc).ok());
    json::Json doc2 = to_json(sample());
    doc2["format"] = "twin-ir/999";
    EXPECT_FALSE(from_json(doc2).ok());
}

TEST(IrCodec, RejectsFloatsAnywhere) {
    json::Json doc = to_json(sample());
    doc["time"]["ticks_per_unit"] = 1000.5;
    EXPECT_FALSE(json::canonical_dump(doc).ok());
    EXPECT_FALSE(from_json(doc).ok());
}

TEST(IrCodec, RejectsDanglingReferences) {
    json::Json doc = to_json(sample());
    doc["transitions"][0]["target"] = "Nowhere";
    EXPECT_FALSE(from_json(doc).ok());
    json::Json doc2 = to_json(sample());
    doc2["locations"][1]["invariant"][0]["clock"] = "z";
    EXPECT_FALSE(from_json(doc2).ok());
}

TEST(IrCodec, HashChangesWithAnySemanticChange) {
    Model m = sample();
    const std::string h0 = ir_sha256(m).value();
    m.transitions[0].guard[0].bound = 3;
    EXPECT_NE(ir_sha256(m).value(), h0);
}

}  // namespace
}  // namespace twin::ir
