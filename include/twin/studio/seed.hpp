/**
 * @file seed.hpp
 * @brief Deterministic demo data: import a reference example and generate telemetry history.
 * @ingroup studio
 *
 * An example directory (e.g. examples/industrial-pump/) contains an
 * `example.json` manifest — assets, relationships, a twin, the five formal
 * artefacts, maintenance history and telemetry *profiles* — plus the artefact
 * files it names. Seeding goes through the ordinary Services use cases, so
 * every artefact is genuinely validated, aligned, compiled and packaged by the
 * real tools; no trust state is ever written directly.
 *
 * Telemetry history is synthesised from generic per-channel profiles (base
 * value, daily cycle, noise, episodes, gaps) with a fixed random seed. These
 * are simulated *observations* (clearly marked as such by their source); they
 * never carry semantic conclusions.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/platform/telemetry.hpp"
#include "twin/studio/services.hpp"

namespace twin::studio {

/// @brief Options for seeding.
struct SeedOptions {
    bool telemetry_history{true};  ///< Generate the history described in the manifest.
    Actor actor{"seed"};           ///< Actor recorded for every seeded operation.
};

/// @brief Import @p example_dir into @p services (idempotence: refuses if the twin already exists).
[[nodiscard]] Result<json::Json> seed_example(Services& services, const std::filesystem::path& example_dir,
                                              const SeedOptions& options = {});

/**
 * @brief Generic profile-driven value generator for one channel (see file documentation).
 *
 * Profile keys: base, dailyAmplitude, noise, min, max, episodes[] with
 * {startHoursAgo, durationMinutes, value, rampMinutes, mode: "set"|"offset", gap}.
 * Times are relative to an anchor ("now" of the generation).
 */
class ProfileGenerator {
public:
    ProfileGenerator(json::Json profile, std::string value_type, std::int64_t anchor_ms, std::uint32_t seed);

    /// @brief Sample at @p t_ms, or std::nullopt inside a gap episode.
    [[nodiscard]] std::optional<platform::TelemetrySample> sample(std::int64_t t_ms);

    /// @brief Baseline-only sample (no episodes): used by the live demo feed after the history.
    [[nodiscard]] platform::TelemetrySample live_sample(std::int64_t t_ms);

private:
    [[nodiscard]] double baseline(std::int64_t t_ms);
    [[nodiscard]] platform::TelemetrySample make(std::int64_t t_ms, double v);
    json::Json profile_;
    std::string type_;
    std::int64_t anchor_ms_;
    std::mt19937 rng_;
    std::normal_distribution<double> normal_{0.0, 1.0};
    std::uniform_real_distribution<double> uniform_{0.0, 1.0};
};

/**
 * @brief Simulated live data source for the demo: appends one sample per channel
 * per expected period, by channel profile, until @p stop is set.
 */
void run_demo_feed(Services& services, const std::filesystem::path& example_dir, const std::atomic<bool>& stop);

}  // namespace twin::studio
