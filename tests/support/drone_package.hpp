/**
 * @file drone_package.hpp
 * @brief Test fixture: builds the indoor-drone Verified Twin Package once per test binary.
 */
#pragma once

#include <gtest/gtest.h>

#include <filesystem>
#include <random>
#include <string>

#include "twin/package/builder.hpp"

namespace twin::test {

/// @brief A fresh, empty temporary directory removed at destruction.
class TempDir {
public:
    TempDir() {
        std::random_device rd;
        path_ = std::filesystem::temp_directory_path() /
                ("twin-test-" + std::to_string(rd()) + std::to_string(rd()));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    [[nodiscard]] std::filesystem::path operator/(const std::string& name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

/// @brief Paths of the indoor-drone models in the source tree.
inline package::BuildInputs drone_build_inputs() {
    const std::filesystem::path m = std::filesystem::path(TWIN_SOURCE_DIR) / "models" / "indoor_drone";
    package::BuildInputs in;
    in.pt_model = m / "V_P_flight_controller.xml";
    in.dt_model = m / "V_D_mission_supervisor.xml";
    in.ontology = m / "domain.ont";
    in.pt_interpretation = m / "pt.interp";
    in.dt_interpretation = m / "dt.interp";
    in.model_id = "indoor-drone-dt";
    in.model_version = "1.0.0";
    return in;
}

/**
 * @brief Suite fixture holding one built package (built once: alignment runs Z3).
 */
class DronePackageSuite : public ::testing::Test {
public:
    static void SetUpTestSuite() {
        dir_ = new TempDir();  // NOLINT(cppcoreguidelines-owning-memory)
        Result<package::BuildResult> built = package::build_package(drone_build_inputs(), dir_->path() / "pkg");
        ASSERT_TRUE(built.ok()) << built.error().to_string();
        package_ = new package::LoadedPackage(built.value().package);  // NOLINT
    }
    static void TearDownTestSuite() {
        delete package_;  // NOLINT
        delete dir_;      // NOLINT
        package_ = nullptr;
        dir_ = nullptr;
    }

protected:
    static const package::LoadedPackage& pkg() { return *package_; }
    static std::filesystem::path pkg_dir() { return dir_->path() / "pkg"; }

private:
    static inline TempDir* dir_ = nullptr;
    static inline package::LoadedPackage* package_ = nullptr;
};

}  // namespace twin::test
