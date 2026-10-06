/**
 * @file telemetry.cpp
 * @brief Telemetry history (see telemetry.hpp).
 */
#include "twin/platform/telemetry.hpp"

#include <algorithm>
#include <cmath>

#include "twin/platform/clock.hpp"

namespace twin::platform {

namespace {

constexpr std::string_view kChannelColumns =
    "id, asset_id, name, value_type, unit, ontology_symbol, source, expected_period_ms, presentation";

Result<TelemetryChannel> read_channel(const Statement& s) {
    TelemetryChannel c;
    c.id = s.text(0);
    c.asset_id = s.text(1);
    c.name = s.text(2);
    c.value_type = s.text(3);
    c.unit = s.text(4);
    c.ontology_symbol = s.opt_text(5);
    c.source = s.text(6);
    c.expected_period_ms = s.integer(7);
    auto p = json::parse(s.text(8));
    if (!p) return std::move(p).error();
    c.presentation = std::move(p).value();
    return c;
}

TelemetrySample read_sample(const Statement& s) {
    return {s.integer(0), s.integer(1), s.opt_real(2), s.opt_text(3), s.text(4), s.opt_integer(5)};
}

bool valid_type(std::string_view t) { return t == "number" || t == "boolean" || t == "category" || t == "string"; }
bool valid_quality(std::string_view q) { return q == "good" || q == "uncertain" || q == "bad"; }

}  // namespace

std::string_view to_string(Freshness f) noexcept {
    switch (f) {
        case Freshness::Fresh: return "fresh";
        case Freshness::Stale: return "stale";
        case Freshness::Missing: return "missing";
        case Freshness::Invalid: return "invalid";
    }
    return "missing";
}

Status TelemetryRepository::upsert_channel(const TelemetryChannel& c) {
    if (c.id.empty() || c.asset_id.empty() || c.name.empty() || !valid_type(c.value_type) || c.expected_period_ms <= 0) {
        return make_error(ErrorCode::InvalidArgument,
                          "channel needs id, asset, name, a value type (number|boolean|category|string) and a "
                          "positive expected period")
            .with("id", c.id);
    }
    auto q = db_.prepare("INSERT INTO telemetry_channels(" + std::string(kChannelColumns) +
                         ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9) ON CONFLICT(id) DO UPDATE SET "
                         "asset_id = excluded.asset_id, name = excluded.name, value_type = excluded.value_type, "
                         "unit = excluded.unit, ontology_symbol = excluded.ontology_symbol, source = excluded.source, "
                         "expected_period_ms = excluded.expected_period_ms, presentation = excluded.presentation");
    if (!q) return std::move(q).error();
    q.value()
        .bind(1, c.id)
        .bind(2, c.asset_id)
        .bind(3, c.name)
        .bind(4, c.value_type)
        .bind(5, c.unit)
        .bind(6, c.ontology_symbol)
        .bind(7, c.source)
        .bind(8, c.expected_period_ms)
        .bind(9, c.presentation.dump());
    return q.value().run();
}

Result<TelemetryChannel> TelemetryRepository::channel(std::string_view id) const {
    auto q = db_.prepare("SELECT " + std::string(kChannelColumns) + " FROM telemetry_channels WHERE id = ?1");
    if (!q) return std::move(q).error();
    q.value().bind(1, id);
    auto row = q.value().step();
    if (!row) return std::move(row).error();
    if (!row.value()) return make_error(ErrorCode::NotFound, "no such telemetry channel").with("id", std::string(id));
    return read_channel(q.value());
}

Result<std::vector<TelemetryChannel>> TelemetryRepository::channels(std::string_view asset_id) const {
    auto q = db_.prepare(asset_id.empty()
                             ? "SELECT " + std::string(kChannelColumns) + " FROM telemetry_channels ORDER BY id"
                             : "SELECT " + std::string(kChannelColumns) +
                                   " FROM telemetry_channels WHERE asset_id = ?1 ORDER BY id");
    if (!q) return std::move(q).error();
    if (!asset_id.empty()) q.value().bind(1, asset_id);
    std::vector<TelemetryChannel> out;
    for (;;) {
        auto row = q.value().step();
        if (!row) return std::move(row).error();
        if (!row.value()) break;
        auto c = read_channel(q.value());
        if (!c) return std::move(c).error();
        out.push_back(std::move(c).value());
    }
    return out;
}

Result<std::int64_t> TelemetryRepository::ingest(std::string_view channel_id,
                                                 const std::vector<TelemetrySample>& samples) {
    auto ch = channel(channel_id);
    if (!ch) return std::move(ch).error();
    const std::string& type = ch.value().value_type;
    for (const auto& s : samples) {
        if (!valid_quality(s.quality)) {
            return make_error(ErrorCode::InvalidArgument, "quality must be good, uncertain or bad")
                .with("channel", std::string(channel_id));
        }
        const bool numeric = type == "number";
        if (numeric && (!s.number || !std::isfinite(*s.number))) {
            return make_error(ErrorCode::InvalidArgument, "numeric channel requires a finite number")
                .with("channel", std::string(channel_id));
        }
        if (type == "boolean" && !(s.text && (*s.text == "true" || *s.text == "false"))) {
            return make_error(ErrorCode::InvalidArgument, "boolean channel requires \"true\" or \"false\"")
                .with("channel", std::string(channel_id));
        }
        if ((type == "category" || type == "string") && !s.text) {
            return make_error(ErrorCode::InvalidArgument, "text channel requires a text value")
                .with("channel", std::string(channel_id));
        }
    }
    Transaction tx(db_);
    if (!tx.begun()) return tx.begun().error();
    auto q = db_.prepare("INSERT INTO telemetry_samples(channel_id, observed_ms, ingested_ms, value_num, value_text, "
                         "quality, logical_ticks) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)");
    if (!q) return std::move(q).error();
    for (const auto& s : samples) {
        std::optional<double> num = s.number;
        if (type == "boolean") num = (*s.text == "true") ? 1.0 : 0.0;
        q.value().reset();
        q.value().bind(1, channel_id).bind(2, s.observed_ms).bind(3, s.ingested_ms);
        if (num) q.value().bind(4, *num);
        else q.value().bind_null(4);
        q.value().bind(5, s.text).bind(6, s.quality).bind(7, s.logical_ticks);
        if (auto st = q.value().run(); !st) return st.error();
    }
    if (auto st = tx.commit(); !st) return st.error();
    return static_cast<std::int64_t>(samples.size());
}

Result<std::optional<TelemetrySample>> TelemetryRepository::latest(std::string_view channel_id) const {
    auto q = db_.prepare("SELECT observed_ms, ingested_ms, value_num, value_text, quality, logical_ticks FROM telemetry_samples "
                         "WHERE channel_id = ?1 ORDER BY observed_ms DESC LIMIT 1");
    if (!q) return std::move(q).error();
    q.value().bind(1, channel_id);
    auto row = q.value().step();
    if (!row) return std::move(row).error();
    if (!row.value()) return std::optional<TelemetrySample>{};
    return std::optional<TelemetrySample>(read_sample(q.value()));
}

Result<std::optional<TelemetrySample>> TelemetryRepository::at(std::string_view channel_id, std::int64_t at_ms) const {
    auto q = db_.prepare("SELECT observed_ms, ingested_ms, value_num, value_text, quality, logical_ticks FROM telemetry_samples "
                         "WHERE channel_id = ?1 AND observed_ms <= ?2 ORDER BY observed_ms DESC LIMIT 1");
    if (!q) return std::move(q).error();
    q.value().bind(1, channel_id).bind(2, at_ms);
    auto row = q.value().step();
    if (!row) return std::move(row).error();
    if (!row.value()) return std::optional<TelemetrySample>{};
    return std::optional<TelemetrySample>(read_sample(q.value()));
}

Result<TelemetrySeries> TelemetryRepository::query(std::string_view channel_id, std::int64_t from_ms,
                                                   std::int64_t to_ms, std::int64_t max_points) const {
    if (to_ms < from_ms) return make_error(ErrorCode::InvalidArgument, "time range end precedes its start");
    if (max_points < 2) return make_error(ErrorCode::InvalidArgument, "maxPoints must be at least 2");
    auto ch = channel(channel_id);
    if (!ch) return std::move(ch).error();
    TelemetrySeries s;
    s.channel_id = channel_id;
    s.from_ms = from_ms;
    s.to_ms = to_ms;
    auto c = db_.prepare("SELECT count(*) FROM telemetry_samples WHERE channel_id = ?1 AND observed_ms BETWEEN ?2 AND ?3");
    if (!c) return std::move(c).error();
    c.value().bind(1, channel_id).bind(2, from_ms).bind(3, to_ms);
    if (auto r = c.value().step(); !r) return std::move(r).error();
    s.total_samples = c.value().integer(0);

    const bool numeric = ch.value().value_type == "number" || ch.value().value_type == "boolean";
    if (s.total_samples <= max_points || !numeric) {
        auto q = db_.prepare("SELECT observed_ms, ingested_ms, value_num, value_text, quality, logical_ticks FROM telemetry_samples "
                             "WHERE channel_id = ?1 AND observed_ms BETWEEN ?2 AND ?3 ORDER BY observed_ms LIMIT ?4");
        if (!q) return std::move(q).error();
        q.value().bind(1, channel_id).bind(2, from_ms).bind(3, to_ms).bind(4, numeric ? max_points : max_points * 10);
        for (;;) {
            auto row = q.value().step();
            if (!row) return std::move(row).error();
            if (!row.value()) break;
            s.samples.push_back(read_sample(q.value()));
        }
        return s;
    }
    s.downsampled = true;
    s.bucket_ms = std::max<std::int64_t>(1, (to_ms - from_ms + max_points) / max_points);
    auto q = db_.prepare(
        "SELECT (observed_ms - ?2) / ?4 AS b, count(*), min(value_num), max(value_num), avg(value_num), "
        "sum(quality = 'bad'), sum(quality = 'uncertain') FROM telemetry_samples "
        "WHERE channel_id = ?1 AND observed_ms BETWEEN ?2 AND ?3 GROUP BY b ORDER BY b");
    if (!q) return std::move(q).error();
    q.value().bind(1, channel_id).bind(2, from_ms).bind(3, to_ms).bind(4, s.bucket_ms);
    for (;;) {
        auto row = q.value().step();
        if (!row) return std::move(row).error();
        if (!row.value()) break;
        const std::int64_t b = q.value().integer(0);
        TelemetryBucket bucket;
        bucket.start_ms = from_ms + b * s.bucket_ms;
        bucket.end_ms = bucket.start_ms + s.bucket_ms;
        bucket.count = q.value().integer(1);
        bucket.min = q.value().real(2);
        bucket.max = q.value().real(3);
        bucket.avg = q.value().real(4);
        bucket.bad = q.value().integer(5);
        bucket.uncertain = q.value().integer(6);
        s.buckets.push_back(bucket);
    }
    return s;
}

Result<Freshness> TelemetryRepository::freshness(const TelemetryChannel& ch, std::int64_t now_ms) const {
    auto last = latest(ch.id);
    if (!last) return std::move(last).error();
    if (!last.value()) return Freshness::Missing;
    if (last.value()->quality == "bad") return Freshness::Invalid;
    return now_ms - last.value()->observed_ms > 3 * ch.expected_period_ms ? Freshness::Stale : Freshness::Fresh;
}

json::Json to_json(const TelemetryChannel& c) {
    return {{"id", c.id},
            {"assetId", c.asset_id},
            {"name", c.name},
            {"valueType", c.value_type},
            {"unit", c.unit},
            {"ontologySymbol", c.ontology_symbol ? json::Json(*c.ontology_symbol) : json::Json(nullptr)},
            {"source", c.source},
            {"expectedPeriodMs", c.expected_period_ms},
            {"observedTimeBasis", c.source.rfind("runtime:", 0) == 0 ? "reception" : "source"},
            {"presentation", c.presentation}};
}

json::Json to_json(const TelemetrySample& s) {
    json::Json j = {{"observedAt", iso8601_utc(s.observed_ms)},
                    {"observedMs", s.observed_ms},
                    {"ingestedAt", iso8601_utc(s.ingested_ms)},
                    {"quality", s.quality}};
    j["logicalTicks"] = s.logical_ticks ? json::Json(*s.logical_ticks) : json::Json(nullptr);
    j["value"] = s.text ? json::Json(*s.text) : s.number ? json::Json(*s.number) : json::Json(nullptr);
    return j;
}

json::Json to_json(const TelemetrySeries& s) {
    json::Json j = {{"channelId", s.channel_id},
                    {"from", iso8601_utc(s.from_ms)},
                    {"to", iso8601_utc(s.to_ms)},
                    {"totalSamples", s.total_samples},
                    {"downsampled", s.downsampled}};
    if (s.downsampled) {
        j["bucketMs"] = s.bucket_ms;
        json::Json buckets = json::Json::array();
        for (const auto& b : s.buckets) {
            buckets.push_back({{"startMs", b.start_ms}, {"endMs", b.end_ms}, {"count", b.count}, {"min", b.min},
                               {"max", b.max}, {"avg", b.avg}, {"bad", b.bad}, {"uncertain", b.uncertain}});
        }
        j["buckets"] = buckets;
    } else {
        json::Json samples = json::Json::array();
        for (const auto& x : s.samples) samples.push_back(to_json(x));
        j["samples"] = samples;
    }
    return j;
}

}  // namespace twin::platform
