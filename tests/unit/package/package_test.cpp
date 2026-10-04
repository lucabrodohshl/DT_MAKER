/**
 * @file package_test.cpp
 * @brief Verified Twin Package: build, verification and tamper detection.
 */
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "support/drone_package.hpp"
#include "twin/core/sha256.hpp"
#include "twin/package/package.hpp"

namespace twin::package {
namespace {

using test::DronePackageSuite;
using test::TempDir;

std::string read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream b;
    b << in.rdbuf();
    return b.str();
}

void write(const std::filesystem::path& p, const std::string& s) {
    std::ofstream(p, std::ios::binary | std::ios::trunc) << s;
}

/// Copy the suite's package into a scratch directory for destructive tests.
std::filesystem::path copy_package(const std::filesystem::path& from, const TempDir& scratch) {
    const std::filesystem::path to = scratch.path() / "copy";
    std::filesystem::copy(from, to, std::filesystem::copy_options::recursive);
    return to;
}

class PackageTest : public DronePackageSuite {};

TEST_F(PackageTest, BuiltPackageVerifies) {
    EXPECT_TRUE(pkg().manifest.aligned);
    EXPECT_TRUE(pkg().manifest.lint_clean);
    EXPECT_TRUE(pkg().manifest.translation_validated);
    EXPECT_TRUE(pkg().manifest.event_deterministic);
    EXPECT_EQ(pkg().model.info.id, "indoor-drone-dt");
    EXPECT_EQ(pkg().package_hash, sha256_hex(read(pkg_dir() / "manifest.json")));
    Result<LoadedPackage> again = load_and_verify(pkg_dir());
    ASSERT_TRUE(again.ok()) << again.error().to_string();
    for (const Check& c : again.value().checks) EXPECT_TRUE(c.passed) << c.name;
}

TEST_F(PackageTest, BuildIsReproducibleUnderSourceDateEpoch) {
    TempDir a;
    TempDir b;
    ::setenv("SOURCE_DATE_EPOCH", "1790000000", 1);  // NOLINT(concurrency-mt-unsafe)
    Result<BuildResult> ra = build_package(test::drone_build_inputs(), a.path() / "p");
    Result<BuildResult> rb = build_package(test::drone_build_inputs(), b.path() / "p");
    ::unsetenv("SOURCE_DATE_EPOCH");  // NOLINT(concurrency-mt-unsafe)
    ASSERT_TRUE(ra.ok() && rb.ok());
    EXPECT_EQ(ra.value().package.package_hash, rb.value().package.package_hash);
    EXPECT_EQ(read(a.path() / "p" / "ir" / "model.ir.json"), read(b.path() / "p" / "ir" / "model.ir.json"));
}

TEST_F(PackageTest, EveryArtefactIsTamperEvident) {
    for (const PackageFile& f : pkg().manifest.files) {
        TempDir scratch;
        const std::filesystem::path dir = copy_package(pkg_dir(), scratch);
        std::string bytes = read(dir / f.path);
        bytes.back() = bytes.back() == ' ' ? '\t' : ' ';  // a one-byte change
        write(dir / f.path, bytes);
        Result<LoadedPackage> r = load_and_verify(dir);
        ASSERT_FALSE(r.ok()) << "tampering with " << f.path << " was not detected";
        EXPECT_EQ(r.error().code, ErrorCode::IntegrityError);
        EXPECT_EQ(r.error().context_value("check"), "file " + f.path + " sha256");
    }
}

TEST_F(PackageTest, ManifestTamperingChangesThePackageIdentity) {
    TempDir scratch;
    const std::filesystem::path dir = copy_package(pkg_dir(), scratch);
    std::string manifest = read(dir / "manifest.json");
    const auto pos = manifest.find("\"version\":\"1.0.0\"");
    ASSERT_NE(pos, std::string::npos);
    manifest.replace(pos, 17, "\"version\":\"1.0.1\"");
    write(dir / "manifest.json", manifest);
    Result<LoadedPackage> r = load_and_verify(dir);
    ASSERT_FALSE(r.ok());  // the IR still says 1.0.0
    EXPECT_EQ(r.error().context_value("check"), "IR model version matches manifest");
}

TEST_F(PackageTest, NonCanonicalManifestIsRejected) {
    TempDir scratch;
    const std::filesystem::path dir = copy_package(pkg_dir(), scratch);
    write(dir / "manifest.json", read(dir / "manifest.json") + "\n");
    Result<LoadedPackage> r = load_and_verify(dir);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().context_value("check"), "manifest.json is canonical JSON");
}

TEST_F(PackageTest, ConsistentlyRehashedForgeryIsCaughtByBindings) {
    // An attacker swaps the ontology and fixes up its hash in the manifest. The
    // file hashes then agree, but the alignment evidence is bound to the
    // original ontology, so the binding check fails.
    TempDir scratch;
    const std::filesystem::path dir = copy_package(pkg_dir(), scratch);
    const std::string forged = read(dir / "semantics" / "domain.ont") + "\naxiom extra : (= cruise_height_m 2)\n";
    write(dir / "semantics" / "domain.ont", forged);
    json::Json m = json::parse(read(dir / "manifest.json")).value();
    for (json::Json& f : m["files"]) {
        if (f["path"] == "semantics/domain.ont") {
            f["sha256"] = sha256_hex(forged);
            f["size"] = static_cast<std::int64_t>(forged.size());
        }
    }
    write(dir / "manifest.json", json::canonical_dump(m).value());
    Result<LoadedPackage> r = load_and_verify(dir);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().context_value("check"), "evidence bound to ontology");
}

TEST_F(PackageTest, PathTraversalInManifestIsRejected) {
    TempDir scratch;
    const std::filesystem::path dir = copy_package(pkg_dir(), scratch);
    json::Json m = json::parse(read(dir / "manifest.json")).value();
    m["files"][0]["path"] = "../outside.txt";
    write(dir / "manifest.json", json::canonical_dump(m).value());
    std::vector<Check> report = verify_report(dir);
    const bool caught = std::any_of(report.begin(), report.end(), [](const Check& c) {
        return !c.passed && c.name.find("path") != std::string::npos;
    });
    EXPECT_TRUE(caught);
}

TEST_F(PackageTest, MissingFileIsReported) {
    TempDir scratch;
    const std::filesystem::path dir = copy_package(pkg_dir(), scratch);
    std::filesystem::remove(dir / "evidence" / "alignment.json");
    EXPECT_FALSE(load_and_verify(dir).ok());
}

TEST_F(PackageTest, UnalignedPairIsNotPackagedByDefault) {
    TempDir out;
    BuildInputs in = test::drone_build_inputs();
    // A DT interpretation that breaks one label equivalence.
    const std::filesystem::path bad = out.path() / "dt_bad.interp";
    std::string interp = read(in.dt_interpretation);
    const auto pos = interp.find("(<= distance_to_waypoint_m waypoint_tolerance_m)");
    ASSERT_NE(pos, std::string::npos);
    interp.replace(pos, 48, "(<= distance_to_waypoint_m 2)");
    write(bad, interp);
    in.dt_interpretation = bad;
    Result<BuildResult> r = build_package(in, out.path() / "pkg");
    ASSERT_FALSE(r.ok());
    in.allow_unaligned = true;
    Result<BuildResult> dev = build_package(in, out.path() / "pkg-dev");
    ASSERT_TRUE(dev.ok()) << dev.error().to_string();
    EXPECT_FALSE(dev.value().package.manifest.aligned && dev.value().package.manifest.lint_clean);
    EXPECT_FALSE(load_and_verify(out.path() / "pkg-dev").ok());  // runtime refuses by default
    EXPECT_TRUE(load_and_verify(out.path() / "pkg-dev", VerifyOptions{true}).ok());
}

TEST_F(PackageTest, ExistingOutputIsNotOverwrittenByDefault) {
    Result<BuildResult> r = build_package(test::drone_build_inputs(), pkg_dir());
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidArgument);
}

}  // namespace
}  // namespace twin::package
