/**
 * @file ledger_test.cpp
 * @brief Tamper evidence: modification, deletion, reordering, insertion,
 *        truncation (anchors), wrong package identity; replay of a tampered ledger.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include <fstream>
#include <sstream>

#include "support/drone_package.hpp"
#include "support/mission_script.hpp"
#include "twin/ledger/replay.hpp"
#include "twin/ledger/verifier.hpp"
#include "twin/runtime/session.hpp"

namespace twin::ledger {
namespace {

using test::DronePackageSuite;
using test::TempDir;

class LedgerTest : public DronePackageSuite {
public:
    static void SetUpTestSuite() {
        DronePackageSuite::SetUpTestSuite();
        dir_ = new TempDir();  // NOLINT
        runtime::SessionOptions o;
        o.ledger_path = dir_->path() / "mission.ledger.jsonl";
        o.deterministic = true;
        o.fsync = false;
        auto s = runtime::TwinSession::start(pkg(), o);
        ASSERT_TRUE(s.ok());
        for (const test::ScriptedInput& step : test::mission_script()) {
            ASSERT_TRUE(s.value()->submit(step.input).ok());
        }
        ASSERT_TRUE(s.value()->close("mission complete").ok());
        std::ifstream in(o.ledger_path, std::ios::binary);
        std::ostringstream b;
        b << in.rdbuf();
        text_ = new std::string(b.str());  // NOLINT
    }
    static void TearDownTestSuite() {
        delete text_;  // NOLINT
        delete dir_;   // NOLINT
        DronePackageSuite::TearDownTestSuite();
    }

protected:
    static const std::string& text() { return *text_; }

    static std::vector<std::string> lines() {
        std::vector<std::string> out;
        std::istringstream in(*text_);
        std::string l;
        while (std::getline(in, l)) out.push_back(l);
        return out;
    }
    static std::string join(const std::vector<std::string>& ls) {
        std::string out;
        for (const std::string& l : ls) out += l + "\n";
        return out;
    }
    static bool has_issue(const VerificationReport& r, const std::string& code) {
        return std::any_of(r.issues.begin(), r.issues.end(), [&](const Issue& i) { return i.code == code; });
    }

private:
    static inline TempDir* dir_ = nullptr;
    static inline std::string* text_ = nullptr;
};

TEST_F(LedgerTest, UntouchedLedgerIsValid) {
    const VerificationReport r = verify_text(text(), VerifyOptions{pkg().package_hash, std::nullopt, true});
    EXPECT_TRUE(r.valid);
    EXPECT_TRUE(r.has_end_record);
    EXPECT_EQ(r.package_hash, pkg().package_hash);
}

TEST_F(LedgerTest, ModificationOfAnyRecordIsDetected) {
    const std::size_t n = lines().size();
    for (std::size_t i = 0; i < n; ++i) {
        const VerificationReport r = tamper_drill(text(), i);
        EXPECT_FALSE(r.valid) << "modification of record " << i << " not detected";
        EXPECT_TRUE(has_issue(r, "hash_mismatch")) << i;
    }
}

TEST_F(LedgerTest, DeletionIsDetected) {
    std::vector<std::string> ls = lines();
    ls.erase(ls.begin() + 4);
    const VerificationReport r = verify_text(join(ls));
    EXPECT_FALSE(r.valid);
    EXPECT_TRUE(has_issue(r, "sequence_gap"));
    EXPECT_TRUE(has_issue(r, "chain_broken"));
}

TEST_F(LedgerTest, ReorderingIsDetected) {
    std::vector<std::string> ls = lines();
    std::swap(ls[3], ls[4]);
    const VerificationReport r = verify_text(join(ls));
    EXPECT_FALSE(r.valid);
    EXPECT_TRUE(has_issue(r, "sequence_gap"));
}

TEST_F(LedgerTest, InsertionIsDetected) {
    std::vector<std::string> ls = lines();
    ls.insert(ls.begin() + 5, ls[5]);  // a replayed (duplicated) record
    const VerificationReport r = verify_text(join(ls));
    EXPECT_FALSE(r.valid);
}

TEST_F(LedgerTest, TruncationIsDetectedByEndRecordAndAnchor) {
    std::vector<std::string> ls = lines();
    const VerificationReport full = verify_text(text());
    const Anchor anchor{full.session, full.head_seq, full.head_hash};
    ls.resize(ls.size() - 3);
    const std::string truncated = join(ls);
    // The chain of a prefix is itself valid ...
    EXPECT_TRUE(verify_text(truncated).valid);
    // ... but a closed session requires its end record, and an anchor pins the head.
    EXPECT_FALSE(verify_text(truncated, VerifyOptions{std::nullopt, std::nullopt, true}).valid);
    const VerificationReport anchored = verify_text(truncated, VerifyOptions{std::nullopt, anchor, false});
    EXPECT_FALSE(anchored.valid);
    EXPECT_TRUE(has_issue(anchored, "anchor_missing"));
}

TEST_F(LedgerTest, RewrittenHistoryIsDetectedByAnchor) {
    const VerificationReport full = verify_text(text());
    const Anchor stale{full.session, 3, std::string(64, 'a')};
    const VerificationReport r = verify_text(text(), VerifyOptions{std::nullopt, stale, false});
    EXPECT_FALSE(r.valid);
    EXPECT_TRUE(has_issue(r, "anchor_mismatch"));
}

TEST_F(LedgerTest, WrongPackageIdentityIsDetected) {
    const VerificationReport r =
        verify_text(text(), VerifyOptions{std::string(64, 'b'), std::nullopt, false});
    EXPECT_FALSE(r.valid);
    EXPECT_TRUE(has_issue(r, "wrong_package"));
}

TEST_F(LedgerTest, TornTailIsReported) {
    const std::string torn = text().substr(0, text().size() - 10);
    EXPECT_FALSE(verify_text(torn).valid);
}

TEST_F(LedgerTest, TamperedLedgerIsNeverReplayed) {
    const std::string tampered = [] {
        std::vector<std::string> ls = lines();
        std::swap(ls[2], ls[3]);
        return join(ls);
    }();
    Result<ReplayReport> r = replay_text(pkg(), tampered);
    ASSERT_TRUE(r.ok());
    EXPECT_FALSE(r.value().chain.valid);
    EXPECT_FALSE(r.value().identical);
}

TEST_F(LedgerTest, ChainHashMatchesTheDocumentedFormula) {
    const std::vector<std::string> ls = lines();
    json::Json first = json::parse(ls[0]).value();
    const std::string body = json::canonical_dump(first["body"]).value();
    EXPECT_EQ(chain_hash(genesis_prev_hash(), body).value(), first["hash"].get<std::string>());
}

}  // namespace
}  // namespace twin::ledger
