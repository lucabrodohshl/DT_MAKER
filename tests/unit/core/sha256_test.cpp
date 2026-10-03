/**
 * @file sha256_test.cpp
 * @brief SHA-256 against FIPS 180-4 example vectors and padding boundaries.
 */
#include <gtest/gtest.h>

#include <random>
#include <string>

#include "twin/core/sha256.hpp"

namespace twin {
namespace {

// FIPS 180-4 / NIST CAVP "SHA-256 short and long message" examples.
TEST(Sha256, FipsExampleVectors) {
    EXPECT_EQ(sha256_hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256_hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(sha256_hex("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
                         "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
              "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST(Sha256, OneMillionA) {
    EXPECT_EQ(sha256_hex(std::string(1'000'000, 'a')),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// Lengths around the 55/56/64-byte padding boundaries (reference: shasum -a 256).
TEST(Sha256, PaddingBoundaries) {
    const std::pair<std::size_t, const char*> cases[] = {
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
        {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
    };
    for (const auto& [n, expected] : cases) {
        EXPECT_EQ(sha256_hex(std::string(n, 'a')), expected) << "length " << n;
    }
}

// Property: incremental hashing with arbitrary chunking equals one-shot hashing.
TEST(Sha256, IncrementalEqualsOneShot) {
    std::mt19937 rng(20261003);
    for (int trial = 0; trial < 200; ++trial) {
        std::string data(static_cast<std::size_t>(rng() % 700), '\0');
        for (char& ch : data) ch = static_cast<char>(rng() & 0xFF);
        Sha256 h;
        std::size_t pos = 0;
        while (pos < data.size()) {
            const std::size_t n = std::min<std::size_t>(rng() % 130, data.size() - pos);
            h.update(std::string_view(data).substr(pos, n));
            pos += n;
        }
        EXPECT_EQ(to_hex(h.finish()), sha256_hex(data));
    }
}

TEST(Sha256, HasherIsReusableAfterFinish) {
    Sha256 h;
    h.update("abc");
    (void)h.finish();
    h.update("abc");
    EXPECT_EQ(to_hex(h.finish()), sha256_hex("abc"));
}

TEST(Sha256, HexRoundTripAndCanonicalForm) {
    const Digest d = sha256("twin");
    Result<Digest> back = digest_from_hex(to_hex(d));
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(back.value(), d);
    EXPECT_TRUE(is_canonical_hex_digest(to_hex(d)));
    EXPECT_FALSE(is_canonical_hex_digest("ABC"));
    std::string upper = to_hex(d);
    for (char& c : upper) c = static_cast<char>(std::toupper(c));
    EXPECT_FALSE(is_canonical_hex_digest(upper));
    EXPECT_TRUE(digest_from_hex(upper).ok());  // parsing accepts either case
    EXPECT_FALSE(digest_from_hex("zz").ok());
}

}  // namespace
}  // namespace twin
