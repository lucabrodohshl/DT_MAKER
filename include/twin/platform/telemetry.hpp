/**
 * @file telemetry.hpp
 * @brief Telemetry channels and history: ingestion, freshness, downsampled queries.
 * @ingroup platform
 *
 * Telemetry are **observations** of the physical system. They are kept here
 * for history, trends and explanation; they are not formal evidence and are
 * never hashed into the execution ledger. Semantic meaning is derived from
 * them only through the ontology services (twin::ontology::evaluate_interpretation)
 * or by the runtime — never by this module and never by the UI.
 *
 * Time stamps:
 *  - `observed_ms`: when the value was observed at the source (event time), in
 *    wall-clock UTC ms. For runtime-fed channels the Physical Twin has no wall
 *    clock, so this is the reception time (the channel view says so);
 *  - `ingested_ms`: when Studio received it (ingestion time, wall clock);
 *  - `logical_ticks`: the source's logical observation time, if it has one
 *    (co-simulated Physical Twin). Never mixed with the wall-clock times.
 *
 * Freshness is classified server-side from the channel's expected period:
 *  - **fresh**: latest sample observed within 3 × expected period of now;
 *  - **stale**: older than that;
 *  - **missing**: no sample at all;
 *  - **invalid**: the latest sample has quality "bad".
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "twin/core/result.hpp"
#include "twin/json/canonical.hpp"
#include "twin/platform/database.hpp"

namespace twin::platform {

/// @brief A telemetry channel (one measured quantity of one asset).
struct TelemetryChannel {
    std::string id;                          ///< e.g. "pump-p101.bearing_temp".
    std::string asset_id;                    ///< Owning asset.
    std::string name;                        ///< Variable id, e.g. "bearing_temp".
    std::string value_type;                  ///< "number" | "boolean" | "category" | "string".
    std::string unit;                        ///< e.g. "degC" (empty if dimensionless).
    std::optional<std::string> ontology_symbol;  ///< Ontology function/relation this observes, if any.
    std::string source;                      ///< Data source, e.g. "opcua://plc-4/ns=2;s=TT101".
    std::int64_t expected_period_ms{1000};   ///< Nominal sampling period.
    json::Json presentation = json::Json::object();  ///< Display-only metadata (label, category, precision, ...).
};

/// @brief One observation.
struct TelemetrySample {
    std::int64_t observed_ms{0};             ///< Event time (UTC ms).
    std::int64_t ingested_ms{0};             ///< Ingestion time (UTC ms).
    std::optional<double> number;            ///< Numeric value (numbers; booleans as 0/1).
    std::optional<std::string> text;         ///< Text value (categories, strings, "true"/"false").
    std::string quality{"good"};             ///< "good" | "uncertain" | "bad".
    std::optional<std::int64_t> logical_ticks;  ///< Logical observation time (ticks), when the source has one.
};

/// @brief Aggregate of the samples in one time bucket (downsampling).
struct TelemetryBucket {
    std::int64_t start_ms{0};   ///< Bucket start (inclusive).
    std::int64_t end_ms{0};     ///< Bucket end (exclusive).
    std::int64_t count{0};      ///< Samples in the bucket.
    double min{0};              ///< Minimum.
    double max{0};              ///< Maximum.
    double avg{0};              ///< Mean.
    std::int64_t bad{0};        ///< Samples with quality "bad".
    std::int64_t uncertain{0};  ///< Samples with quality "uncertain".
};

/// @brief Result of a history query.
struct TelemetrySeries {
    std::string channel_id;                 ///< Channel.
    std::int64_t from_ms{0};                ///< Requested range start.
    std::int64_t to_ms{0};                  ///< Requested range end.
    std::int64_t total_samples{0};          ///< Samples in range.
    bool downsampled{false};                ///< True: `buckets` is set, otherwise `samples`.
    std::int64_t bucket_ms{0};              ///< Bucket width when downsampled.
    std::vector<TelemetrySample> samples;   ///< Raw samples (ascending observed time).
    std::vector<TelemetryBucket> buckets;   ///< Aggregates (empty buckets omitted = gaps).
};

/// @brief Freshness classification (see file documentation).
enum class Freshness { Fresh, Stale, Missing, Invalid };
/// @brief "fresh" / "stale" / "missing" / "invalid".
[[nodiscard]] std::string_view to_string(Freshness f) noexcept;

/// @brief Telemetry store (see file documentation).
class TelemetryRepository {
public:
    /// @brief Repository over @p db.
    explicit TelemetryRepository(Database& db) : db_(db) {}

    /// @brief Insert or replace a channel definition.
    [[nodiscard]] Status upsert_channel(const TelemetryChannel& channel);
    /// @brief One channel.
    [[nodiscard]] Result<TelemetryChannel> channel(std::string_view id) const;
    /// @brief Channels of an asset (all channels if @p asset_id is empty), ordered by id.
    [[nodiscard]] Result<std::vector<TelemetryChannel>> channels(std::string_view asset_id) const;

    /// @brief Append samples (one transaction). Validates value kind against the channel type.
    [[nodiscard]] Result<std::int64_t> ingest(std::string_view channel_id, const std::vector<TelemetrySample>& samples);

    /// @brief Latest sample by observation time.
    [[nodiscard]] Result<std::optional<TelemetrySample>> latest(std::string_view channel_id) const;

    /// @brief Latest sample observed at or before @p at_ms (for replay/explanations at a past instant).
    [[nodiscard]] Result<std::optional<TelemetrySample>> at(std::string_view channel_id, std::int64_t at_ms) const;

    /**
     * @brief Samples in [from_ms, to_ms]; if more than @p max_points, aggregated into
     * at most @p max_points equal-width buckets (min/max/avg, quality counts).
     */
    [[nodiscard]] Result<TelemetrySeries> query(std::string_view channel_id, std::int64_t from_ms, std::int64_t to_ms,
                                                std::int64_t max_points) const;

    /// @brief Freshness of a channel at @p now_ms.
    [[nodiscard]] Result<Freshness> freshness(const TelemetryChannel& channel, std::int64_t now_ms) const;

private:
    Database& db_;
};

/// @brief API form of a channel (unit, source, ontology symbol, presentation).
[[nodiscard]] json::Json to_json(const TelemetryChannel& channel);
/// @brief API form of a sample (observation and ingestion time, quality, value).
[[nodiscard]] json::Json to_json(const TelemetrySample& sample);
/// @brief API form of a series (raw samples or downsampled buckets).
[[nodiscard]] json::Json to_json(const TelemetrySeries& series);

}  // namespace twin::platform
